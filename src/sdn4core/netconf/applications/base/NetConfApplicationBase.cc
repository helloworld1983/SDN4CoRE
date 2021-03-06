//
// c Timo Haeckel, for HAW Hamburg
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include "NetConfApplicationBase.h"

#include <string>

//INET
#include "inet/networklayer/common/L3AddressResolver.h"

namespace SDN4CoRE {

Define_Module(NetConfApplicationBase);

#define SELFMESSAGE_SEND_NETCONF "Send Netconf"
#define SELFMESSAGE_SEND_HELLO "Send Hello"

void NetConfApplicationBase::initialize() {
    cXMLElement* xmlServerConnections = par("serverConnections").xmlValue();
    if (xmlServerConnections) {
        if (strcmp(xmlServerConnections->getName(), "server_connections")
                == 0) {
            cXMLElementList applicationsXML =
                    xmlServerConnections->getChildrenByTagName("application");
            for (size_t i = 0; i < applicationsXML.size(); i++) {
                const char* clientAppHost = applicationsXML[i]->getAttribute(
                        "client_host");
                const char* clientAppIndex = applicationsXML[i]->getAttribute(
                        "client_app");
                const char* realHost = getParentModule()->getFullName();
                if (clientAppIndex && clientAppHost
                        && (atoi(clientAppIndex) == getIndex())
                        && (strcmp(realHost, clientAppHost) == 0)) {
                    cXMLElementList connectionsXML =
                            applicationsXML[i]->getChildrenByTagName(
                                    "connection");
                    for (size_t i = 0; i < connectionsXML.size(); i++) {
                        const char* localPort = connectionsXML[i]->getAttribute(
                                "local_port");
                        const char* remoteAddress =
                                connectionsXML[i]->getAttribute(
                                        "remote_address");
                        const char* remotePort =
                                connectionsXML[i]->getAttribute("remote_port");
                        const char* connectAt = connectionsXML[i]->getAttribute(
                                "connect_at");

                        if (localPort && remoteAddress && remotePort
                                && connectAt) {
                            // set the connection and push_back to array.
                            Connections_t connection;
                            connection.localPort = atoi(localPort);
                            connection.remoteAddress = remoteAddress;
                            connection.remotePort = atoi(remotePort);
                            connection.connectAt = std::stod(connectAt);
                            connection.state =
                                    ConnectionState_t::ConnectionStateWaiting;

                            // check if there are any configurations.
                            cXMLElementList configuresXML =
                                    connectionsXML[i]->getChildrenByTagName(
                                            "configure");
                            for (auto configureXML : configuresXML) {
                                const char* executeAt =
                                        configureXML->getAttribute("at");
                                const char* configType =
                                        configureXML->getAttribute("type");
                                if (executeAt && configType && getConfigTypeFor(configType) >= 0) {
                                    Configurations_t* config = new Configurations_t();
                                    config->executeAt = std::stod(executeAt);
                                    config->type = (NetConfApplicationBase::NetConfMessageType_t) getConfigTypeFor(configType);
                                    config->state =
                                            ConfigurationState_t::ConfigurationStateWaiting;
                                    switch(config->type){
                                    case NetConfMessageType_t::NetConfMessageType_EditConfig:
                                        config->data = getConfigDataFor(configureXML);
                                        config->filter = getConfigFilterFor(configureXML);
                                        if(const char* target = configureXML->getAttribute("target")){
                                            config->target = target;
                                        } else {
                                            config->target = "running";
                                        }
                                        break;

                                    case NetConfMessageType_t::NetConfMessageType_GetConfig:
                                        config->filter = getConfigFilterFor(configureXML);
                                        if(const char* target = configureXML->getAttribute("target")){
                                            config->target = target;
                                        } else {
                                            config->target = "running";
                                        }
                                        break;

                                    case NetConfMessageType_t::NetConfMessageType_CopyConfig:
                                        if(const char* target = configureXML->getAttribute("target")){
                                            config->target = target;
                                        } else {
                                            config->target = "candidate";
                                        }
                                        if(const char* source = configureXML->getAttribute("source")){
                                            config->source = source;
                                        } else {
                                            config->source = "running";
                                        }
                                        break;

                                    case NetConfMessageType_t::NetConfMessageType_DeleteConfig:
                                        if(const char* target = configureXML->getAttribute("target")){
                                            config->target = target;
                                        } else {
                                            throw cRuntimeError("No configStore specified to be deleted.");
                                        }
                                        break;
                                    default:
                                        break;
                                    }

                                    connection.configurations.push_back(config);
                                }
                            }

                            _connections.push_back(connection);
                        }
                    }
                }
            }
        }
    }

    scheduleNextConnection();
}

int NetConfApplicationBase::getConfigTypeFor(const char* type) {
    if (!strcmp(type, "edit_config")) {
        return NetConfMessageType::NetConfMessageType_EditConfig;
    } else if (!strcmp(type, "get_config")) {
        return NetConfMessageType::NetConfMessageType_GetConfig;
    } else if (!strcmp(type, "copy_config")) {
        return NetConfMessageType::NetConfMessageType_CopyConfig;
    } else if (!strcmp(type, "delete_config")) {
        return NetConfMessageType::NetConfMessageType_DeleteConfig;
    }
    return -1;
}

NetConfConfig* NetConfApplicationBase::getConfigDataFor(cXMLElement* element) {
    return new NetConfConfig();
}

NetConfFilter* NetConfApplicationBase::getConfigFilterFor(
        cXMLElement* element) {
    return new NetConfFilter();
}

void NetConfApplicationBase::scheduleNextConnection() {
    int index = -1;
    SimTime next = SimTime::getMaxTime();
    for (size_t i = 0; i < _connections.size(); i++) {
        auto connection = _connections[i];
        if (connection.connectAt < next && connection.connectAt >= simTime()
                && (connection.state
                        == ConnectionState_t::ConnectionStateWaiting)) {
            index = i;
            next = connection.connectAt;
        }
    }

    if (index > -1) {
        _connections[index].state = ConnectionState_t::ConnectionStateScheduled;
        cMessage* msg = new cMessage(SELFMESSAGE_SEND_HELLO);
        msg->setContextPointer(&_connections[index]);
        scheduleAt(next, msg);
    }
}

void NetConfApplicationBase::scheduleNextConfigurationFor(
        Connections_t* connection) {
    if (connection
            && (connection->state
                    == ConnectionState_t::ConnectionStateEstablished)) {
        int index = -1;
        SimTime next = SimTime::getMaxTime();
        for (size_t i = 0; i < connection->configurations.size(); i++) {
            auto configuration = connection->configurations[i];
            if (configuration->executeAt < next
                    && configuration->executeAt >= simTime()
                    && (configuration->state
                            == ConfigurationState_t::ConfigurationStateWaiting)) {
                index = i;
                next = configuration->executeAt;
            }
        }

        if (index > -1) {
            NetConfMessage_RPC* rpc = createNetConfRPCForConfiguration(
                    connection, index);
            if (rpc) {
                rpc->setContextPointer(connection);
                cMessage* msg = new cMessage(SELFMESSAGE_SEND_NETCONF);
                msg->setContextPointer(rpc);
                scheduleAt(next, msg);
                connection->configurations[index]->state =
                        ConfigurationState_t::ConfigurationStateScheduled;
            } else {
                connection->configurations[index]->state =
                        ConfigurationState_t::ConfigurationStateError;
            }
        }
    }
}

void NetConfApplicationBase::handleMessage(cMessage *msg) {
    if (msg->isSelfMessage()) {
        if (strcmp(msg->getName(), SELFMESSAGE_SEND_HELLO) == 0) {
            Connections_t* connection =
                    static_cast<Connections_t*>(msg->getContextPointer());

            if (connection) {
                send(createHelloFor(connection), gate("applicationOut"));
                connection->state = ConnectionState_t::ConnectionStateRequested;
            }

            scheduleNextConnection();
        } else if (strcmp(msg->getName(), SELFMESSAGE_SEND_NETCONF) == 0) {
            NetConfMessage_RPC* rpc = static_cast<NetConfMessage_RPC*> (msg->getContextPointer());

            if(rpc){
                Connections_t* connection =
                                    static_cast<Connections_t*>(rpc->getContextPointer());
                NetConfCtrlInfo* ctrl = dynamic_cast<NetConfCtrlInfo*>(rpc->getControlInfo());
                if(ctrl && connection){
                    connection->configurations[atoi(ctrl->getMessage_id())]->state = ConfigurationState_t::ConfigurationStateRequested;
                    send(rpc, gate("applicationOut"));

                    scheduleNextConfigurationFor(connection);
                }
            }
        }
    } else if (NetConfHello* hello = dynamic_cast<NetConfHello*>(msg)) {
        NetConfClientSessionInfoTCP* sessionInfo =
                (NetConfClientSessionInfoTCP*) hello->getContextPointer();
        Connections_t* connection = mapSessionInfoToConnection(sessionInfo);
        if (connection) {
            connection->session_id = sessionInfo->getSessionId();
            connection->state = ConnectionState_t::ConnectionStateEstablished;
            scheduleNextConfigurationFor(connection);
        }
    } else if (NetConfMessage_RPCReply* reply = dynamic_cast<NetConfMessage_RPCReply*>(msg)){
        if(NetConfCtrlInfo* info = dynamic_cast<NetConfCtrlInfo*>(reply->getControlInfo())){

                Connections_t* found = nullptr;
                for (size_t i = 0; i < _connections.size(); i++) {
                    auto& connection = _connections[i];
                    if (connection.state
                            == ConnectionState_t::ConnectionStateEstablished
                            && connection.session_id == info->getSession_id()) {
                        found = &connection;
                    }
                }
                if(found){
                    if(dynamic_cast<NetConf_RPCReplyElement_Ok*>(reply->getEncapsulatedPacket())){
                        found->configurations[atoi(info->getMessage_id())]->state = ConfigurationState_t::ConfigurationStateSuccess;
                    } else if(dynamic_cast<NetConf_RPCReplyElement_Error*>(reply->getEncapsulatedPacket())) {
                        found->configurations[atoi(info->getMessage_id())]->state = ConfigurationState_t::ConfigurationStateError;
                    }
                }
        }
    }

    delete msg;
}

NetConfHello* NetConfApplicationBase::createHelloFor(
        Connections_t* connection) {
    NetConfHello* hello = new NetConfHello("NetConfHello");
    hello->setByteLength(4);

    NetConfClientCtrlInfo_Connection* ctrl =
            new NetConfClientCtrlInfo_Connection();
    ctrl->setConnectAddress(connection->remoteAddress);
    ctrl->setConnectPort(connection->remotePort);
    ctrl->setLocalPort(connection->localPort);

    hello->setControlInfo(ctrl);
    return hello;
}

NetConfOperation_EditConfig* NetConfApplicationBase::createEditConfigOperation(
        Configurations_t* config) {
    NetConfOperation_EditConfig* editconfig =
                        new NetConfOperation_EditConfig();
    editconfig->setTarget(config->target.c_str());
    editconfig->encapsulate(config->data);
    editconfig->setDefaultOperation(
            NetConfOperation_Operation::NETCONFOPERATION_OPERATION_MERGE);
    editconfig->setErrorOption(
            NetConfOperation_ErrorOption::NETCONFOPERATION_ERROROPTION_CONTINUEONERROR);
    editconfig->setByteLength(
            sizeof(editconfig->getTarget())
                    + config->data->getByteSize()
                    + sizeof(editconfig->getDefaultOperation()
                            + sizeof(editconfig->getErrorOption())));
    return editconfig;
}

NetConfOperation_GetConfig* NetConfApplicationBase::createGetConfigOperation(
        Configurations_t* config) {
    NetConfOperation_GetConfig* getconfig =
                        new NetConfOperation_GetConfig();
    getconfig->setSource(config->source.c_str());
    getconfig->setFilter(*(config->filter));
    getconfig->setByteLength(
            sizeof(getconfig->getSource())
                    + config->filter->getByteSize());
    return getconfig;
}

NetConfOperation_CopyConfig* NetConfApplicationBase::createCopyConfigOperation(
        Configurations_t* config) {
    NetConfOperation_CopyConfig* copyconfig =
                        new NetConfOperation_CopyConfig();
    copyconfig->setSource(config->source.c_str());
    copyconfig->setTarget(config->target.c_str());
    copyconfig->setByteLength(
            sizeof(copyconfig->getSource())
                    + sizeof(copyconfig->getTarget()));
    return copyconfig;
}

NetConfOperation_DeleteConfig* NetConfApplicationBase::createDeleteConfigOperation(
        Configurations_t* config) {
    NetConfOperation_DeleteConfig* deleteconfig =
                        new NetConfOperation_DeleteConfig();
    deleteconfig->setTarget(config->target.c_str());
    deleteconfig->setByteLength(
                    sizeof(deleteconfig->getTarget()));
    return deleteconfig;
}

NetConfCtrlInfo* NetConfApplicationBase::createControlInfo(int messageType, int sessionId,
        const char* messageId) {
    NetConfCtrlInfo* ctrl = new NetConfCtrlInfo();
    ctrl->setMessageType(messageType);
    ctrl->setSession_id(sessionId);
    ctrl->setMessage_id(messageId);
    return ctrl;
}

NetConfMessage_RPC* NetConfApplicationBase::createNetConfRPCForConfiguration(
        Connections_t* connection, int index) {
    auto config = connection->configurations[index];
    NetConfMessage_RPC* rpc = new NetConfMessage_RPC();
    rpc->setMessage_id(std::to_string(index).c_str());
    rpc->setByteLength(
            sizeof(rpc->getMessageType())
                    + sizeof(rpc->getMessage_id()));
    if (config) {
        switch (config->type) {
        case NetConfMessageType_EditConfig:
            rpc->setName("RCP EditConfig");
            rpc->encapsulate(createEditConfigOperation(config));
            break;

        case NetConfMessageType_GetConfig:
            rpc->setName("RCP GetConfig");
            rpc->encapsulate(createGetConfigOperation(config));
            break;

        case NetConfMessageType_CopyConfig:
            rpc->setName("RCP CopyConfig");
            rpc->encapsulate(createCopyConfigOperation(config));
            break;

        case NetConfMessageType_DeleteConfig:
            rpc->setName("RCP DeleteConfig");
            rpc->encapsulate(createDeleteConfigOperation(config));
            break;

        default:
            throw cRuntimeError("Can't create RPC for configuration: NetConfMessageType unknown.");
            break;
        }
    }
    rpc->setControlInfo(createControlInfo(rpc->getMessageType(), connection->session_id, rpc->getMessage_id()));
    return rpc;
}

NetConfApplicationBase::Connections_t* NetConfApplicationBase::mapSessionInfoToConnection(
        NetConfClientSessionInfoTCP* sessionInfo) {
    if (sessionInfo) {
        for (size_t i = 0; i < _connections.size(); i++) {
            auto& connection = _connections[i];
            if (connection.state
                    == ConnectionState_t::ConnectionStateEstablished
                    && connection.session_id == sessionInfo->getSessionId()) {
                return &connection;
            }
            //connection not yet established so use ip address
            else if ((connection.session_id == -1)
                    && (strcmp(
                            inet::L3AddressResolver().resolve(
                                    connection.remoteAddress).str().c_str(),
                            sessionInfo->getSocket()->getRemoteAddress().str().c_str())
                            == 0)) {
                return &connection;
            }
        }
    }
    return nullptr;
}

}  // namespace SDN4CoRE
