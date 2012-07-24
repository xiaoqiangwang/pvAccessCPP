/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvAccessCPP is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
 
#ifndef CLIENTFACTORY_H
#define CLIENTFACTORY_H

namespace epics {
namespace pvAccess { 

class ClientFactory {
    public:
    static void start();
    static void stop();
};

}}

#endif  /* CLIENTFACTORY_H */
