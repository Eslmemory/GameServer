#include "iomanager.h"
#include "address.h"
#include "socket.h"
#include "hook.h"
#include <unistd.h>
#include <vector>

void Print1() {
    printf("==================this is fiber1==================\n");
    for (int i = 0; i < 10; i++)
        printf("%d\n", i);
}

void Print2() {
    printf("==================this is fiber2==================\n");
    for (int i = 10; i < 20; i++)
        printf("%d\n", i);
}

void Print3() {
    printf("==================this is fiber3==================\n");
    for (int i = 20; i < 30; i++)
        printf("%d\n", i);
}

void Print4() {
    printf("==================this is fiber4==================\n");
    for (int i = 30; i < 40; i++)
        printf("%d\n", i);
}

void TestConnect() {
    WebServer::IPAddress::ipAddressPtr addr = WebServer::Address::LookupAnyIpAddress("www.baidu.com");
    if (addr) {
        std::cout << "get address " << addr->toString();
    }
    else {
        std::cout << "get address failed\n";
        return;
    }

    WebServer::Socket::socketPtr sock = WebServer::Socket::CreateTCP(addr);
    addr->setPort(80);
    WebServer::setHookEnable(true);
    if (!sock->connect(addr, 100)) {
        std::cout << "connect " << addr->toString() << " failed" << std::endl;
        return;
    }
    else {
        std::cout << "connect " << addr->toString() << " success" << std::endl;
    }

    sock->cancelWrite();
    const char buff[] = "GET / HTTP/1.0\r\n\r\n";
    int rt = sock->send(buff, sizeof(buff));
    if (rt <= 0) {
        std::cout << "send failed rt=" << rt << std::endl;
        return;
    }

    std::string buffs;
    buffs.resize(4096);
    rt = sock->receive(&buffs[0], buffs.size());
    if (rt <= 0) {
        std::cout << "receive failed rt=" << rt << std::endl;
        return;
    }

    buffs.resize(rt);
    std::cout << buffs << std::endl;
}


int main(int argc, char* argv[])
{
    // WebServer::IOManager::ioManagerPtr ptr = std::make_shared<WebServer::IOManager>(2);
    // ptr->addTimer(3000, []() {
    //     printf("timer!!\n");
    // }, false);
    WebServer::IOManager iom;
    iom.schedule(&TestConnect);

    return 0;
}