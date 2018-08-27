/*
 * Copyright information and license terms for this software can be
 * found in the file LICENSE that is included with the distribution
 */

#include <list>

#include <epicsMutex.h>
#include <epicsGuard.h>
#include <errlog.h>

#include <shareLib.h>
#include <pv/sharedPtr.h>
#include <pv/noDefaultMethods.h>
#include <pv/sharedVector.h>
#include <pv/bitSet.h>
#include <pv/pvData.h>
#include <pv/createRequest.h>
#include <pv/status.h>
#include <pv/reftrack.h>

#define epicsExportSharedSymbols
#include "sharedstateimpl.h"


namespace {
struct MailboxHandler : public pvas::SharedPV::Handler {
    virtual ~MailboxHandler() {}
    virtual void onPut(const pvas::SharedPV::shared_pointer& self, pvas::Operation& op) OVERRIDE FINAL
    {
        self->post(op.value(), op.changed());
        op.info("Set!");
        op.complete();
    }
    static std::tr1::shared_ptr<pvas::SharedPV::Handler> build() {
        std::tr1::shared_ptr<MailboxHandler> ret(new MailboxHandler);
        return ret;
    }
};
} // namespace

namespace pvas {

SharedPV::Config::Config()
    :dropEmptyUpdates(true)
    ,mapperMode(pvd::PVRequestMapper::Mask)
{}

size_t SharedPV::num_instances;

SharedPV::shared_pointer SharedPV::build(const std::tr1::shared_ptr<Handler>& handler, Config *conf)
{
    assert(!!handler);
    SharedPV::shared_pointer ret(new SharedPV(handler, conf));
    ret->internal_self = ret;
    return ret;
}

SharedPV::shared_pointer SharedPV::buildReadOnly(Config *conf)
{
    SharedPV::shared_pointer ret(new SharedPV(std::tr1::shared_ptr<Handler>(), conf));
    ret->internal_self = ret;
    return ret;
}

SharedPV::shared_pointer SharedPV::buildMailbox(pvas::SharedPV::Config *conf)
{
    std::tr1::shared_ptr<Handler> handler(new MailboxHandler);
    SharedPV::shared_pointer ret(new SharedPV(handler, conf));
    ret->internal_self = ret;
    return ret;
}

SharedPV::SharedPV(const std::tr1::shared_ptr<Handler> &handler, pvas::SharedPV::Config *conf)
    :config(conf ? *conf : Config())
    ,handler(handler)
    ,debugLvl(0)
{
    REFTRACE_INCREMENT(num_instances);
}

SharedPV::~SharedPV() {
    close();
    REFTRACE_DECREMENT(num_instances);
}

void SharedPV::setHandler(const std::tr1::shared_ptr<Handler>& handler)
{
    Guard G(mutex);
    this->handler = handler;
}

SharedPV::Handler::shared_pointer SharedPV::getHandler() const
{
    Guard G(mutex);
    return handler;
}


bool SharedPV::isOpen() const
{
    Guard G(mutex);
    return !!type;
}

namespace {
struct PutInfo { // oh to be able to use std::tuple ...
    std::tr1::shared_ptr<SharedPut> put;
    pvd::StructureConstPtr type;
    pvd::Status status;
    PutInfo(const std::tr1::shared_ptr<SharedPut>& put, const pvd::StructureConstPtr& type, const pvd::Status& status)
        :put(put), type(type), status(status)
    {}
    PutInfo(const std::tr1::shared_ptr<SharedPut>& put, const pvd::StructureConstPtr& type, const std::string& message)
        :put(put), type(type)
    {
        if(!message.empty())
            status = pvd::Status::warn(message);
    }
};
}

void SharedPV::open(const pvd::PVStructure &value, const epics::pvData::BitSet& valid)
{
    typedef std::vector<PutInfo> xputs_t;
    typedef std::vector<std::tr1::shared_ptr<SharedRPC> > xrpcs_t;
    typedef std::vector<std::tr1::shared_ptr<pva::MonitorFIFO> > xmonitors_t;
    typedef std::vector<std::tr1::shared_ptr<pva::GetFieldRequester> > xgetfields_t;

    const pvd::StructureConstPtr newtype(value.getStructure());

    xputs_t p_put;
    xrpcs_t p_rpc;
    xmonitors_t p_monitor;
    xgetfields_t p_getfield;
    {
        Guard I(mutex);

        if(type)
            throw std::logic_error("Already open()");

        p_put.reserve(puts.size());
        p_rpc.reserve(rpcs.size());
        p_monitor.reserve(monitors.size());
        p_getfield.reserve(getfields.size());

        type = value.getStructure();
        current = pvd::getPVDataCreate()->createPVStructure(newtype);
        current->copyUnchecked(value);
        this->valid = valid;

        FOR_EACH(puts_t::const_iterator, it, end, puts) {
            try {
                try {
                    (*it)->mapper.compute(*current, *(*it)->pvRequest, config.mapperMode);
                    p_put.push_back(PutInfo((*it)->shared_from_this(), (*it)->mapper.requested(), (*it)->mapper.warnings()));
                }catch(std::runtime_error& e) {
                    // compute() error
                    p_put.push_back(PutInfo((*it)->shared_from_this(), pvd::StructureConstPtr(), pvd::Status::error(e.what())));
                }
            }catch(std::tr1::bad_weak_ptr&) {
                //racing destruction
            }
        }
        FOR_EACH(rpcs_t::const_iterator, it, end, rpcs) {
            try {
                p_rpc.push_back((*it)->shared_from_this());
            }catch(std::tr1::bad_weak_ptr&) {}
        }
        FOR_EACH(monitors_t::const_iterator, it, end, monitors) {
            try {
                (*it)->open(newtype);
                // post initial update
                (*it)->post(*current, valid);
                p_monitor.push_back((*it)->shared_from_this());
            }catch(std::tr1::bad_weak_ptr&) {}
        }
        // consume getField
        FOR_EACH(getfields_t::iterator, it, end, getfields) {
            p_getfield.push_back(it->lock());
        }
       getfields.clear(); // consume
    }
    // unlock for callbacks
    FOR_EACH(xputs_t::iterator, it, end, p_put) {
        SharedPut::requester_type::shared_pointer requester(it->put->requester.lock());
        if(requester) {
            if(it->status.getType()==pvd::Status::STATUSTYPE_WARNING)
                requester->message(it->status.getMessage(), pvd::warningMessage);
            requester->channelPutConnect(it->status, it->put, it->type);
        }
    }
    FOR_EACH(xrpcs_t::iterator, it, end, p_rpc) {
        SharedRPC::requester_type::shared_pointer requester((*it)->requester.lock());
        if(requester) requester->channelRPCConnect(pvd::Status(), *it);
    }
    FOR_EACH(xmonitors_t::iterator, it, end, p_monitor) {
        (*it)->notify();
    }
    FOR_EACH(xgetfields_t::iterator, it, end, p_getfield) {
        if(*it) (*it)->getDone(pvd::Status(), newtype);
    }
}

void SharedPV::open(const epics::pvData::PVStructure& value)
{
    // consider all fields to have non-default values.  For users how don't keep track of this.
    open(value, pvd::BitSet().set(0));
}

void SharedPV::open(const pvd::StructureConstPtr& type)
{
    pvd::PVStructurePtr value(pvd::getPVDataCreate()->createPVStructure(type));
    open(*value);
}

void SharedPV::close(bool destroy)
{
    typedef std::vector<std::tr1::shared_ptr<pva::ChannelPutRequester> > xputs_t;
    typedef std::vector<std::tr1::shared_ptr<pva::ChannelRPCRequester> > xrpcs_t;
    typedef std::vector<std::tr1::shared_ptr<pva::MonitorFIFO> > xmonitors_t;
    typedef std::vector<std::tr1::shared_ptr<SharedChannel> > xchannels_t;

    xputs_t p_put;
    xrpcs_t p_rpc;
    xmonitors_t p_monitor;
    xchannels_t p_channel;
    {
        Guard I(mutex);

        if(!type)
            return;

        p_put.reserve(puts.size());
        p_rpc.reserve(rpcs.size());
        p_monitor.reserve(monitors.size());
        p_channel.reserve(channels.size());

        FOR_EACH(puts_t::const_iterator, it, end, puts) {
            (*it)->mapper.reset();
            p_put.push_back((*it)->requester.lock());
        }
        FOR_EACH(rpcs_t::const_iterator, it, end, rpcs) {
            p_rpc.push_back((*it)->requester.lock());
        }
        FOR_EACH(monitors_t::const_iterator, it, end, monitors) {
            (*it)->close();
            p_monitor.push_back((*it)->shared_from_this());
        }
        FOR_EACH(channels_t::const_iterator, it, end, channels) {
            p_channel.push_back((*it)->shared_from_this());
        }

        type.reset();
        current.reset();
        if(destroy) {
            // forget about all clients, to prevent the possibility of our
            // sending a second destroy notification.
            puts.clear();
            rpcs.clear();
            monitors.clear();
            channels.clear();
        }
    }
    FOR_EACH(xputs_t::iterator, it, end, p_put) {
        if(*it) (*it)->channelDisconnect(destroy);
    }
    FOR_EACH(xrpcs_t::iterator, it, end, p_rpc) {
        if(*it) (*it)->channelDisconnect(destroy);
    }
    FOR_EACH(xmonitors_t::iterator, it, end, p_monitor) {
        (*it)->notify();
    }
    FOR_EACH(xchannels_t::iterator, it, end, p_channel) {
        pva::ChannelRequester::shared_pointer req((*it)->requester.lock());
        if(!req) continue;
        req->channelStateChange(*it, destroy ? pva::Channel::DESTROYED : pva::Channel::DISCONNECTED);
    }
}

pvd::PVStructure::shared_pointer SharedPV::build()
{
    Guard G(mutex);
    if(!type)
        throw std::logic_error("Can't build() before open()");
    return pvd::getPVDataCreate()->createPVStructure(type);
}

void SharedPV::post(const pvd::PVStructure& value,
                    const pvd::BitSet& changed)
{
    typedef std::vector<std::tr1::shared_ptr<pva::MonitorFIFO> > xmonitors_t;
    xmonitors_t p_monitor;
    {
        Guard I(mutex);

        if(!type)
            throw std::logic_error("Not open()");
        else if(*type!=*value.getStructure())
            throw std::logic_error("Type mis-match");

        if(current) {
            current->copyUnchecked(value, changed);
            valid |= changed;
        }

        p_monitor.reserve(monitors.size()); // ick, for lack of a list with thread-safe iteration

        FOR_EACH(monitors_t::const_iterator, it, end, monitors) {
            (*it)->post(value, changed);
            p_monitor.push_back((*it)->shared_from_this());
        }
    }
    FOR_EACH(xmonitors_t::iterator, it, end, p_monitor) {
        (*it)->notify();
    }
}

void SharedPV::fetch(epics::pvData::PVStructure& value, epics::pvData::BitSet& valid)
{
    Guard I(mutex);
    if(!type)
        throw std::logic_error("Not open()");
    else if(value.getStructure()!=type)
        throw std::logic_error("Types do not match");

    value.copy(*current);
    valid = this->valid;
}


std::tr1::shared_ptr<pva::Channel>
SharedPV::connect(const std::tr1::shared_ptr<epics::pvAccess::ChannelProvider> &provider,
                  const std::string &channelName,
                  const std::tr1::shared_ptr<pva::ChannelRequester>& requester)
{
    shared_pointer self(internal_self);
    std::tr1::shared_ptr<SharedChannel> ret(new SharedChannel(self, provider, channelName, requester));
    return ret;
}

void SharedPV::setDebug(int lvl)
{
    Guard G(mutex);
    debugLvl = lvl;
}

int SharedPV::isDebug() const
{
    Guard G(mutex);
    return debugLvl;
}

} // namespace pvas
