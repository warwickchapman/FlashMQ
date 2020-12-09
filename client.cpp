#include "client.h"

#include <cstring>
#include <sstream>

Client::Client(int fd, ThreadData_p threadData) :
    fd(fd),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    readbuf = (char*)malloc(CLIENT_BUFFER_SIZE);
    writebuf = (char*)malloc(CLIENT_BUFFER_SIZE);
}

Client::~Client()
{
    epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL); // NOTE: the last NULL can cause crash on old kernels
    close(fd);
    free(readbuf);
}

// false means any kind of error we want to get rid of the client for.
bool Client::readFdIntoBuffer()
{
    int read_size = getReadBufMaxWriteSize();

    int n;
    while ((n = read(fd, &readbuf[wi], read_size)) != 0)
    {
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                return false;
        }

        wi += n;

        if (getReadBufBytesUsed() >= readBufsize)
        {
            growReadBuffer();
        }

        read_size = getReadBufMaxWriteSize();
    }

    if (n == 0) // client disconnected.
    {
        return false;
    }

    return true;
}

void Client::writeMqttPacket(MqttPacket &packet)
{
    if (packet.getSize() > getWriteBufMaxWriteSize())
        growWriteBuffer(packet.getSize());

    std::memcpy(&writebuf[wwi], &packet.getBites()[0], packet.getSize());
    wwi += packet.getSize();
}

bool Client::writeBufIntoFd() // TODO: ignore the signal BROKEN PIPE we now also get when a client disappears.
{
    int n;
    while ((n = write(fd, &writebuf[wri], getWriteBufBytesUsed())) != 0)
    {
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                return false;
        }

        wri += n;
    }

    if (wri == wwi)
    {
        wri = 0;
        wwi = 0;
    }

    return true;
}

std::string Client::repr()
{
    std::ostringstream a;
    a << "Client = " << clientid << ", user = " << username;
    return a.str();
}

bool Client::bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn)
{
    while (getReadBufBytesUsed() >= MQTT_HEADER_LENGH)
    {
        // Determine the packet length by decoding the variable length
        size_t remaining_length_i = 1;
        int multiplier = 1;
        size_t packet_length = 0;
        unsigned char encodedByte = 0;
        do
        {
            if (remaining_length_i >= getReadBufBytesUsed())
                break;
            encodedByte = readbuf[remaining_length_i++];
            packet_length += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            if (multiplier > 128*128*128)
                return false;
        }
        while ((encodedByte & 128) != 0);
        packet_length += remaining_length_i;

        if (!authenticated && packet_length >= 1024*1024)
        {
            throw ProtocolError("An unauthenticated client sends a packet of 1 MB or bigger? Probably it's just random bytes.");
        }

        if (packet_length <= getReadBufBytesUsed())
        {
            MqttPacket packet(&readbuf[ri], packet_length, remaining_length_i, this);
            packetQueueIn.push_back(std::move(packet));

            ri += packet_length;
        }
        else
            break;

    }

    if (ri == wi)
    {
        ri = 0;
        wi = 0;
    }

    return true;

    // TODO: reset buffer to normal size after a while of not needing it, or not needing the extra space.
}

void Client::setClientProperties(const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive)
{
    this->clientid = clientId;
    this->username = username;
    this->connectPacketSeen = connectPacketSeen;
    this->keepalive = keepalive;
}













