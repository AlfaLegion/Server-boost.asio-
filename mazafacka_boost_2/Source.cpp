#include<boost\asio.hpp>
#include<boost\lexical_cast.hpp>
#include<boost\thread.hpp>
#include<thread>
#include<vector>
#include<mutex>
#include<thread>
#include<iostream>

#include <WinSock2.h>
#include<Ws2def.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include<mstcpip.h.>


#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>


using namespace boost::asio;
using Socket = ip::tcp::socket;
using socket_ptr = boost::shared_ptr<Socket>;

class client;
class Server;

class client:public std::enable_shared_from_this<client>
{
private:
	Socket sock;
	mutable std::mutex mut_client;
	std::condition_variable con_var_client;
	std::string name;
	mutable std::mutex mut_socket;
	std::string id_client;
	bool end_flag;
public:
	client(io_service& io) :sock(io),end_flag(false)
	{

	}
	size_t available()const
	{
		return sock.available();
	}
	std::string read(size_t sBuff)
	{

		std::shared_ptr<char>buff(new char[sBuff]);
		std::unique_lock<std::mutex>un_lock(mut_socket, std::try_to_lock);
		sock.read_some(buffer(buff.get(), sBuff));
		un_lock.unlock();
		return std::string(buff.get(), sBuff);
	}
	const std::string& get_name()const
	{
		return name;
	}
	void write(const std::string& str)
	{
		std::lock_guard<std::mutex>lg(mut_socket);
		sock.write_some(buffer(str));
	}
	
	void set_end_flag()
	{
		end_flag = true;
	}
	Socket& get_socket()
	{
		return sock;
	}
	void set_id_client(const std::string& _id)
	 {
		 id_client = _id;
	 }
	const std::string& get_id_client()const
	 {
		 return id_client;
	 }
	void set_name_client()
	{

		auto size_buff = sock.available();
		std::shared_ptr<char>buff(new char[size_buff]);
		sock.read_some(buffer(buff.get(),size_buff));
		name=std::string(buff.get(), size_buff);
	}
	void close()
	{
		std::lock_guard<std::mutex>lg(mut_socket);
		sock.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
		sock.close();
	}
	void notify_one()
	{
		con_var_client.notify_one();
	}
	void wait()
	{
		std::unique_lock<std::mutex>un_lock(mut_client);
		con_var_client.wait(un_lock);
	}
	bool get_end_flag()const
	{
		return end_flag;
	}
	std::shared_ptr<client> get_shared_ptr()
	{
		return shared_from_this();
	}
	~client()
	{
		this->close();
	}
};

class Server
{
private:
	unsigned short nPort;
	ip::tcp::endpoint ep;
	ip::tcp::acceptor acc;
	mutable std::mutex mut_Clients;
	std::list<std::shared_ptr<client>>Clients;
	std::vector<std::string>comands;
private:
	void remove(const std::string& id_cl)
	{
		std::cout << "exit" << std::endl;
		auto serch = std::find_if(Clients.begin(), Clients.end(), [&](std::shared_ptr<client>& cl) {return cl->get_id_client()== id_cl; });
		serch->get()->set_end_flag();
		serch->get()->notify_one();

		//Clients.erase(serch);
	}

	void execute_command(const std::string& command, std::shared_ptr<client>& cl)
	{
		std::string data;
		std::stringstream buff_string;
		if (command == "#count")
		{
			std::lock_guard<std::mutex>lg(mut_Clients);
			buff_string << Clients.size();
			buff_string >> data;
			cl->write(data);
		}
		if (command == "#users")
		{
			std::lock_guard<std::mutex>lg(mut_Clients);
			for (auto &x : Clients)
				data += x->get_name() + "; ";
			cl->write(data);
		}
		if (command == "#id")
		{
			buff_string << cl->get_id_client();
			buff_string >> data;
			cl->write(data);
		}
		if (command == "#exit")
		{
			std::lock_guard<std::mutex>lg(mut_Clients);
			remove(cl->get_id_client());
		}
	}
	void process_only_client(client& clnt)
	{
		clnt.set_id_client(boost::lexical_cast<std::string>(std::this_thread::get_id()));
		while (true)
		{
			clnt.wait();
			std::string str(clnt.read(clnt.available()));
			show(str);
			//handler_msg(str, clnt.get_shared_ptr());
		}
	}  // отдельный поток для каждого клиента, работа клиента производится в рамках этой функции
	bool is_valid_client(std::shared_ptr<client>& cl)
	{
		

	}

public:
	Server(io_service& io, unsigned short __nport, ip::tcp type) :ep(type, __nport), acc(io, ep)
	{
		acc.listen();
		comands = { "#count","#users","#id","#exit" };
	}
	size_t count()const
	{
		std::lock_guard<std::mutex>lg(mut_Clients);
		return Clients.size();
	}
	void accept(std::shared_ptr<client> cl)
	{
		acc.accept(cl->get_socket());
		std::lock_guard<std::mutex>lg(mut_Clients);
		Clients.push_back(move(cl));
		std::thread th(&Server::process_only_client, this, std::ref(*Clients.back()));
		th.detach();
	}
	void handler_activ()// обработка активности клиента
	{
		/*struct tcp_keepalive alive;
		alive.onoff = 1;
		alive.keepaliveinterval = 2000;
		alive.keepalivetime = 20000;
		DWORD dwBytesRet = 0;*/
		while (true)
		{
			std::lock_guard<std::mutex>lg(mut_Clients);
			for (auto& x : Clients)
			{
				/*if (WSAIoctl(x->get_socket().native_handle(), SIO_KEEPALIVE_VALS, &alive, sizeof(alive), NULL, 0, &dwBytesRet, NULL, NULL))
				{
					
					std::cout << "error " <<WSAGetLastError()<< std::endl;
				}
				*/
				if (x->available())
				{
					x->notify_one();
				}
			}
		}
	}
	void handler_msg(const std::string& data, std::shared_ptr<client>& cl)//обработчик команд
	{
		enum class States { start, command, legal_command, unknow, messange };
		States state = States::start;
		bool flag = true;
		while (flag)
		{
			switch (state)
			{
			case States::start:
			{
				if (data[0] == '#')
					state = States::command;
				else state = States::messange;
				break;
			}
			case States::command:
			{
				bool ex_state_flag = true;
				for (auto& x : comands)
				{
					if (x == data)
					{
						ex_state_flag = false;
						state = States::legal_command;
						break;
					}
				}
				if (ex_state_flag)
					state = States::unknow;
				break;
			}
			case States::legal_command:
			{
				execute_command(data, cl);   // команда верна то её отправляем на исполнение
				state = States::start;
				flag = false;
				break;
			}
			case States::unknow:
			{
				state = States::messange;
				break;
			}
			case States::messange:
			{
				show(data);//
				state = States::start;
				flag = false;
				break;
			}
			default:
				break;
			}
		}
	}

	void show(const std::string& str)//для отладки вывод на экран сервера
	{
		std::cout << str << std::endl;
	}

	void connect_database(const std::string& ip_adress)
	{

	}
};

int main()
{	
	
	setlocale(LC_ALL, "Rus");
	io_service service;
	try
	{
	Server serv(service, 2002, ip::tcp::v4());
	std::thread th(&Server::handler_activ,&serv);
	th.detach();
		while (true)
		{

			std::shared_ptr<client>_client(new client(service));
			serv.accept(_client);
		}
	}
	catch(const boost::system::system_error& er)
	{
		std::cout << er.what() << std::endl;
	}
	system("pause");
}
