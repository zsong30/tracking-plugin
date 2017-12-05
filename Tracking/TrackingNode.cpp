/*
    ------------------------------------------------------------------

    This file is part of the Tracking plugin for the Open Ephys GUI
    Written by:

    Alessio Buccino     alessiob@ifi.uio.no
    Mikkel Lepperod
    Svenn-Arne Dragly

    Center for Integrated Neuroplasticity CINPLA
    Department of Biosciences
    University of Oslo
    Norway

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "TrackingNode.h"
#include "TrackingNodeEditor.h"
#include "TrackingMessage.h"

//preallocate memory for msg
#define BUFFER_MSG_SIZE 256

using namespace std;

TrackingNode::TrackingNode()
    : GenericProcessor ("Tracking Port")
    , m_startingRecTimeMillis (0)
    , m_startingAcqTimeMillis (0)
    , m_positionIsUpdated (false)
    , m_isRecordingTimeLogged (false)
    , m_isAcquisitionTimeLogged (false)
    , m_received_msg (0)
{
    setProcessorType (PROCESSOR_TYPE_SOURCE);
    sendSampleCount = false;

    TrackingModule module;
    module.address = "/red";
    module.port = 27020;
    module.color = "red";
    module.messageQueue = new TrackingQueue();
    //    module.server = new TrackingServer(module.port, module.address);
    module.server = new TrackingServer(module.port, module.address);
    module.server->addProcessor(this);
    module.server->startThread();

    trackingModules.add (module);

    lastNumInputs = 0;

}

TrackingNode::~TrackingNode()
{
//    for (int i; i< trackingModules.size (); i++)
//    {
//        TrackingModule& current  = trackingModules.getReference(i);
//        std::cout << "Removing source " << i << std::endl;
//        current.server->stop();
//    }
}

AudioProcessorEditor* TrackingNode::createEditor()
{
    editor = new TrackingNodeEditor (this, true);
    return editor;
}

//Since the data needs a maximum buffer size but the actual number of read bytes might be less, let's
//add that info as a metadata field.
void TrackingNode::updateSettings()
{
    moduleEventChannels.clear();
    for (int i = 0; i < trackingModules.size(); i++)
    {
        //It's going to be raw binary data, so let's make it uint8
        EventChannel* chan = new EventChannel (EventChannel::UINT8_ARRAY, 1, 24, CoreServices::getGlobalSampleRate(), this);
        chan->setName ("Tracking data");
        chan->setDescription ("Tracking data received from Bonsai. x, y, width, height");
        chan->setIdentifier ("external.tracking.rawData");
        chan->addEventMetaData(new MetaDataDescriptor(MetaDataDescriptor::CHAR, 15, "Color", "Tracking source color to be displayed", "channelInfo.extra"));
        eventChannelArray.add (chan);
    }
    lastNumInputs = getNumInputs();
}

void TrackingNode::addSource (int port, String address, String color)
{
    TrackingModule module;
    module.address = address;
    module.port = port;
    module.color = color;
    module.messageQueue = new TrackingQueue();

    if (!isPortUsed(port))
    {
        try
        {
            module.server = new TrackingServer(module.port, module.address);
            module.server->addProcessor(this);
            module.server->startThread();
        }
        catch (const std::runtime_error& e)
        {
            std::cout << "Add source: " << e.what() << std::endl;
        }
    }
    else
    {
        module.server = new TrackingServer();
        module.server->addProcessor(this);
    }

    trackingModules.add (module);
}

void TrackingNode::removeSource (int i)
{
    TrackingModule& current  = trackingModules.getReference(i);
    delete current.server;
    trackingModules.remove(i);
}

bool TrackingNode::isPortUsed(int port)
{
    bool used = false;
    for (int i = 0; i < trackingModules.size (); i++)
    {
        TrackingModule& current  = trackingModules.getReference(i);
        if (current.port == port)
            used = true;
    }
    return used;
}

void TrackingNode::setPort (int i, int port)
{
    if (i < trackingModules.size ())
    {
        TrackingModule& module = trackingModules.getReference (i);
        module.port = port;

        // reinstantiate server
        delete module.server;
        try
        {
            module.server = new TrackingServer(module.port, module.address);
            module.server->addProcessor(this);
            module.server->startThread();
        }
        catch (const std::runtime_error& e)
        {
            std::cout << "Set port: " << e.what() << std::endl;
        }
    }
}

int TrackingNode::getPort(int i)
{
    if (i < trackingModules.size ())
    {
        TrackingModule& module = trackingModules.getReference (i);
        return module.port;
    }
    else
        return -1;
}

void TrackingNode::setAddress (int i, String address)
{
    if (i < trackingModules.size ())
    {
        TrackingModule& module = trackingModules.getReference (i);
        module.address = address;

        // reinstantiate server
        delete module.server;
        try
        {
            module.server = new TrackingServer(module.port, module.address);
            module.server->addProcessor(this);
            module.server->startThread();
        }
        catch (const std::runtime_error& e)
        {
            std::cout << "Set address: " <<e.what() << std::endl;
        }

    }
}

String TrackingNode::getAddress(int i)
{
    if (i < trackingModules.size ())
    {
        TrackingModule& module = trackingModules.getReference (i);
        return module.address;
    }
    else
        return "";
}

void TrackingNode::setColor (int i, String color)
{
    if (i < trackingModules.size ())
    {
        TrackingModule& module = trackingModules.getReference (i);
        module.color = color;
    }
}

String TrackingNode::getColor(int i)
{
    if (i < trackingModules.size ())
    {
        TrackingModule& module = trackingModules.getReference (i);
        return module.color;
    }
    else
        return "";
}

void TrackingNode::process (AudioSampleBuffer&)
{
    if (!m_positionIsUpdated)
    {
        return;
    }

    lock.enter();

    for (int i = 0; i < trackingModules.size (); i++)
    {
        TrackingModule& module = trackingModules.getReference (i);
        while (true) {
            auto *message = module.messageQueue->pop ();
            if (!message) {
                break;
            }

            setTimestampAndSamples (uint64(message->timestamp), 0);
            MetaDataValueArray metadata;
            MetaDataValuePtr color = new MetaDataValue(MetaDataDescriptor::CHAR, 15);
            color->setValue(module.color.toLowerCase());
            metadata.add(color);
            const EventChannel* chan = getEventChannel (getEventChannelIndex (i, getNodeId()));
            BinaryEventPtr event = BinaryEvent::createBinaryEvent (chan,
                                                                   message->timestamp,
                                                                   reinterpret_cast<uint8_t *>(message),
                                                                   sizeof(TrackingData),
                                                                   metadata);
            addEvent (chan, event, 0);
        }
    }

    lock.exit();
    m_positionIsUpdated = false;

}

int TrackingNode::getTrackingModuleIndex(int port, String address)
{
    int index = -1;
    for (int i = 0; i < trackingModules.size (); i++)
    {
        TrackingModule& current  = trackingModules.getReference(i);
        if (current.port == port && current.address.compare(address) == 0)
            index = i;
    }
    return index;
}

int TrackingNode::getNSources()
{
    return trackingModules.size ();
}

void TrackingNode::receiveMessage (int port, String address, const TrackingData &message)
{
    int index = getTrackingModuleIndex(port, address);
    if (index != -1)
    {
        TrackingModule& selectedModule = trackingModules.getReference (index);

        lock.enter();

        if (CoreServices::getRecordingStatus())
        {
            if (!m_isRecordingTimeLogged)
            {
                m_received_msg = 0;
                m_startingRecTimeMillis =  Time::currentTimeMillis();
                m_isRecordingTimeLogged = true;
                std::cout << "Starting Recording Ts: " << m_startingRecTimeMillis << std::endl;
                selectedModule.messageQueue->clear();
                CoreServices::sendStatusMessage ("Clearing queue before start recording");
            }
        }
        else
        {
            m_isRecordingTimeLogged = false;
        }


        if (CoreServices::getAcquisitionStatus()) // && !CoreServices::getRecordingStatus())
        {
            if (!m_isAcquisitionTimeLogged)
            {
                m_startingAcqTimeMillis = Time::currentTimeMillis();
                m_isAcquisitionTimeLogged = true;
                std::cout << "Starting Acquisition at Ts: " << m_startingAcqTimeMillis << std::endl;
                selectedModule.messageQueue->clear();
                CoreServices::sendStatusMessage ("Clearing queue before start acquisition");
            }

            m_positionIsUpdated = true;

            int64 ts = CoreServices::getGlobalTimestamp();

            TrackingData outputMessage = message;
            outputMessage.timestamp = ts;
            selectedModule.messageQueue->push (outputMessage);
            m_received_msg++;
        }
        else
            m_isAcquisitionTimeLogged = false;

        lock.exit();
    }

}

bool TrackingNode::isReady()
{
    return true;
}

void TrackingNode::saveCustomParametersToXml (XmlElement* parentElement)
{
    XmlElement* mainNode = parentElement->createNewChildElement ("TrackingNode");
    for (int i = 0; i < trackingModules.size(); i++)
    {
        TrackingModule& module = trackingModules.getReference (i);
        XmlElement* source = new XmlElement("Source_"+String(i+1));
        source->setAttribute ("port", module.port);
        source->setAttribute ("address", module.address);
        source->setAttribute ("color", module.color);
        mainNode->addChildElement(source);
    }
}

void TrackingNode::loadCustomParametersFromXml ()
{
    trackingModules.clear();
    if (parametersAsXml != nullptr)
    {
        forEachXmlChildElement (*parametersAsXml, mainNode)
        {
            if (mainNode->hasTagName ("TrackingNode"))
            {
                forEachXmlChildElement(*mainNode, source)
                {
                    int port = source->getIntAttribute("port");
                    String address = source->getStringAttribute("address");
                    String color = source->getStringAttribute("color");

                    addSource (port, address, color);
                }
            }
        }
    }
}

// Class TrackingQueue methods
TrackingQueue::TrackingQueue()
    : m_head (-1)
    , m_tail (-1)
{
    memset (m_buffer, 0, BUFFER_SIZE);
}

TrackingQueue::~TrackingQueue() {}

void TrackingQueue::push (const TrackingData &message)
{
    m_head = (m_head + 1) % BUFFER_SIZE;
    m_buffer[m_head] = message;
}

TrackingData* TrackingQueue::pop ()
{
    if (isEmpty())
        return nullptr;

    m_tail = (m_tail + 1) % BUFFER_SIZE;
    return &(m_buffer[m_tail]);

}

bool TrackingQueue::isEmpty()
{
    return m_head == m_tail;
}

void TrackingQueue::clear()
{
    m_tail = -1;
    m_head = -1;
}


// Class TrackingServer methods
TrackingServer::TrackingServer ()
    : Thread ("OscListener Thread")
    , m_listeningSocket (IpEndpointName (), this)
{
}

TrackingServer::TrackingServer (int port, String address)
    : Thread ("OscListener Thread")
    , m_incomingPort (port)
    , m_address (address)
    , m_listeningSocket (IpEndpointName ("localhost", m_incomingPort), this)
{
}

TrackingServer::~TrackingServer()
{
    // stop the OSC Listener thread running
    m_listeningSocket.AsynchronousBreak();
    // allow the thread 2 seconds to stop cleanly - should be plenty of time.
    stopThread (2000);
}

void TrackingServer::ProcessMessage (const osc::ReceivedMessage& receivedMessage,
                                     const IpEndpointName&)
{
    try
    {
        uint32 argumentCount = 4;

        if ( receivedMessage.ArgumentCount() != argumentCount) {
            cout << "ERROR: TrackingServer received message with wrong number of arguments. "
                 << "Expected " << argumentCount << ", got " << receivedMessage.ArgumentCount() << endl;
            return;
        }

        for (uint32 i = 0; i < receivedMessage.ArgumentCount(); i++)
        {
            if (receivedMessage.TypeTags()[i] != 'f')
            {
                cout << "TrackingServer only support 'f' (floats), not '"
                     << receivedMessage.TypeTags()[i] << "'" << endl;
                return;
            }
        }

        osc::ReceivedMessageArgumentStream args = receivedMessage.ArgumentStream();

        TrackingData trackingData;

        // Arguments:
        args >> trackingData.position.x; // 0 - x
        args >> trackingData.position.y; // 1 - y
        args >> trackingData.position.width; // 2 - box width
        args >> trackingData.position.height; // 3 - box height
        args >> osc::EndMessage;

        for (TrackingNode* processor : m_processors)
        {
            //            String address = processor->address();

            if ( std::strcmp ( receivedMessage.AddressPattern(), m_address.toStdString().c_str() ) != 0 )
            {
                continue;
            }
            // add trackingmodule to receive message call: processor->receiveMessage (m_incomingPort, m_address, trackingData);
            processor->receiveMessage (m_incomingPort, m_address, trackingData);
        }
    }
    catch ( osc::Exception& e )
    {
        // any parsing errors such as unexpected argument types, or
        // missing arguments get thrown as exceptions.
        DBG ("error while parsing message: " << receivedMessage.AddressPattern() << ": " << e.what() << "\n");
    }
}

void TrackingServer::addProcessor (TrackingNode* processor)
{
    m_processors.push_back (processor);
}

void TrackingServer::removeProcessor (TrackingNode* processor)
{
    m_processors.erase (std::remove (m_processors.begin(), m_processors.end(), processor), m_processors.end());
}

void TrackingServer::run()
{
    // Start the oscpack OSC Listener Thread
    m_listeningSocket.Run();
}

void TrackingServer::stop()
{
    // Stop the oscpack OSC Listener Thread
    if (isThreadRunning())
    {
        m_listeningSocket.AsynchronousBreak();
    }
}