#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include "client_http.h"
#include <mutex>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>

#include "http_parser.h"



class glob_data
{

    std::mutex smutext;
    std::string host;
    std::string name;

    public:
    static glob_data& Instance();

    private:
    glob_data (const glob_data&){}

    static glob_data m_instance;
    glob_data();
    ~glob_data();
    public :
    std::string get_host()
    {
        std::string data ;
        smutext.lock();
        data = glob_data::host;
        smutext.unlock();
        return data ;
    }

    std::string get_name()
    {
        std::string data ;
        smutext.lock();
        data = glob_data::name;
        smutext.unlock();
        return data ;
    }


    void set_data(std::string phost,std::string pname)
    {
        smutext.lock();
        glob_data::host=phost;
        glob_data::name=pname;
        smutext.unlock();
    }



};

glob_data glob_data::m_instance=glob_data();

glob_data::glob_data()
{
    std::cout<<"Creation"<<std::endl;
}

glob_data::~glob_data()
{
    std::cout<<"Destruction"<<std::endl;
}

glob_data& glob_data::Instance()
{
    return m_instance;
}


namespace tcp_proxy
{
    namespace ip = boost::asio::ip;

    class bridge : public boost::enable_shared_from_this<bridge>
    {
        public:

            typedef ip::tcp::socket socket_type;
            typedef boost::shared_ptr<bridge> ptr_type;

            bridge(boost::asio::io_service& ios)
                : downstream_socket_(ios),
                upstream_socket_(ios)
        {

        }

            socket_type& downstream_socket()
            {
                return downstream_socket_;
            }

            socket_type& upstream_socket()
            {
                return upstream_socket_;
            }

            void start(const std::string& upstream_host, unsigned short upstream_port)
            {
                upstream_socket_.async_connect(
                        ip::tcp::endpoint(
                            boost::asio::ip::address::from_string(upstream_host),
                            upstream_port),
                        boost::bind(&bridge::handle_upstream_connect,
                            shared_from_this(),
                            boost::asio::placeholders::error));
            }

            void handle_upstream_connect(const boost::system::error_code& error)
            {
                if (!error)
                {
                    upstream_socket_.async_read_some(
                            boost::asio::buffer(upstream_data_,max_data_length),
                            boost::bind(&bridge::handle_upstream_read,
                                shared_from_this(),
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));

                    downstream_socket_.async_read_some(
                            boost::asio::buffer(downstream_data_,max_data_length),
                            boost::bind(&bridge::handle_downstream_read,
                                shared_from_this(),
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
                }
                else
                    close();
            }

        private:

            void handle_downstream_write(const boost::system::error_code& error)
            {
                if (!error)
                {
                    //std::cout << "DOWNSTREAM" <<  downstream_data_ << std::endl;
                    upstream_socket_.async_read_some(
                            boost::asio::buffer(upstream_data_,max_data_length),
                            boost::bind(&bridge::handle_upstream_read,
                                shared_from_this(),
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
                }
                else
                    close();
            }

            void handle_downstream_read(const boost::system::error_code& error,
                    const size_t& bytes_transferred)
            {
                if (!error)
                {
                    async_write(upstream_socket_,
                            boost::asio::buffer(downstream_data_,bytes_transferred),
                            boost::bind(&bridge::handle_upstream_write,
                                shared_from_this(),
                                boost::asio::placeholders::error));
                }
                else
                    close();
            }

            void handle_upstream_write(const boost::system::error_code& error)
            {
                if (!error)
                {
                    //print raw data incoming

                    std::cout <<"### HEADER ###\n"<<   downstream_data_ << "##########\n"<< std::endl;
                   
                }
                else
                    close();
            }

            void handle_upstream_read(const boost::system::error_code& error,
                    const size_t& bytes_transferred)
            {
                if (!error)
                {
                    async_write(downstream_socket_,
                            boost::asio::buffer(upstream_data_,bytes_transferred),
                            boost::bind(&bridge::handle_downstream_write,
                                shared_from_this(),
                                boost::asio::placeholders::error));
                }
                else
                    close();
            }

            void close()
            {
                boost::mutex::scoped_lock lock(mutex_);
                if (downstream_socket_.is_open())
                    downstream_socket_.close();
                if (upstream_socket_.is_open())
                    upstream_socket_.close();
            }

            socket_type downstream_socket_;
            socket_type upstream_socket_;

            enum { max_data_length = 8192 }; //8KB
            unsigned char downstream_data_[max_data_length];
            unsigned char upstream_data_[max_data_length];
            boost::mutex mutex_;

        public:
            class acceptor
            {
                public:

                    acceptor(boost::asio::io_service& io_service,
                            const std::string& local_host, unsigned short local_port,
                            const std::string& upstream_host, unsigned short upstream_port,std::string auth_host,std::string auth_page)
                        : io_service_(io_service),
                        localhost_address(boost::asio::ip::address_v4::from_string(local_host)),
                        acceptor_(io_service_,ip::tcp::endpoint(localhost_address,local_port)),
                        upstream_port_(upstream_port),
                        upstream_host_(upstream_host)
                {this->auth_host=auth_host;this->auth_page=auth_page;}

                    bool accept_connections()
                    {
                        try
                        {
                            session_ = boost::shared_ptr<bridge>(new bridge(io_service_));
                            acceptor_.async_accept(session_->downstream_socket(),
                                    boost::bind(&acceptor::handle_accept,
                                        this,
                                        boost::asio::placeholders::error));
                        }
                        catch(std::exception& e)
                        {
                            std::cerr << "acceptor exception: " << e.what() << std::endl;
                            return false;
                        }
                        return true;
                    }

                private:

                    void handle_accept(const boost::system::error_code& error)
                    {
                        if (!error)
                        {
                            session_->start(upstream_host_,upstream_port_);
                            if (!accept_connections())
                            {
                                std::cerr << "Failure during call to accept." << std::endl;
                            }
                        }
                        else
                        {
                            std::cerr << "Error: " << error.message() << std::endl;
                        }
                    }

                    boost::asio::io_service& io_service_;
                    ip::address_v4 localhost_address;
                    ip::tcp::acceptor acceptor_;
                    ptr_type session_;
                    unsigned short upstream_port_;
                    std::string upstream_host_;
                    std::string auth_host;
                    std::string auth_page;
                public :
                    inline void set_auth_host(std::string str_host){this->auth_host=str_host;}
                    inline void set_auth_page(std::string str_page){this->auth_page=str_page;}


            };

    };
}

int main(int argc, char* argv[])
{
    if (argc != 8)
    {
        std::cerr << "usage: tcpproxy_server <ip proxy> <port proxy> <ip apache> <port apache> <user id> <auth host> <auth page>" << std::endl;
        return 1;
    }


    const unsigned short local_port   = static_cast<unsigned short>(::atoi(argv[2]));
    const unsigned short forward_port = static_cast<unsigned short>(::atoi(argv[4]));
    const std::string local_host      = argv[1];
    const std::string forward_host    = argv[3];
    int uid=atoi( argv[5] );
    glob_data::Instance().set_data(argv[6],argv[7]);

    boost::asio::io_service ios;

    try
    {
        tcp_proxy::bridge::acceptor acceptor(ios,
                local_host, local_port,
                forward_host, forward_port,argv[6],argv[7]);
        acceptor.accept_connections();

        if (setuid(uid)!=0)
        {
            printf("cannot set uid %d exit(-1)\n",uid);
            exit(-1);
        }
        printf("set uid %d\n",uid);
        ios.run();
    }
    catch(std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

/*
 * [Note] On posix systems the tcp proxy server build command is as follows:
 * c++ -pedantic -ansi -Wall -Werror -O3 -o tcpproxy_server tcpproxy_server.cpp -L/usr/lib -lstdc++ -lpthread -lboost_thread -lboost_system
 */
