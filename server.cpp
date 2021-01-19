#include<boost/asio.hpp>
#include<nlohmann/json.hpp>
#include<SQLAPI.h>
#include<spdlog/spdlog.h>


#include<atomic>
#include<iostream>
#include<fstream>
#include<memory>
#include<thread>
#include<sstream>

using json = nlohmann::json;
using namespace boost;

class Service {
public:
  Service(std::shared_ptr<asio::ip::tcp::socket> sock, std::shared_ptr<SAConnection> con):
    m_sock(sock),
    m_con(con) {
      m_sign_choice.reset(new asio::streambuf);
      m_login_buf.reset(new asio::streambuf);
      m_password_buf.reset(new asio::streambuf);
    }

  void StartHandling() {
    asio::async_write(*m_sock.get(), asio::buffer("Sign in [1] or Sign up [2]: \n"),
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onSignUpRequestSent(ec, bytes_transferred); 
        });
  }

private:
  void onSignUpRequestSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onSignUpRequestSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    asio::async_read_until(*m_sock.get(), *(m_sign_choice.get()), '\n',
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onSignUpResponseReceived(ec, bytes_transferred);
        });
  }

  void onSignUpResponseReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onSignUpResponseReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    
    std::istream istrm(m_sign_choice.get());
    std::getline(istrm, sign_choice);
    m_sign_choice.reset(new asio::streambuf);

    if (sign_choice == "1" || sign_choice == "2") {
      asio::async_write(*m_sock.get(), asio::buffer("Enter login: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onLoginRequestSent(ec, bytes_transferred); 
          });
    } else {
      asio::async_write(*m_sock.get(), asio::buffer("Input incorrect. Enter 1 or 2: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onSignUpRequestSent(ec, bytes_transferred); 
          });
    } 
  }

  void onLoginRequestSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onLoginRequestSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 

    spdlog::info("Login request sent");
    asio::async_read_until(*m_sock.get(), *(m_login_buf.get()), '\n',
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onLoginReceived(ec, bytes_transferred);
        });
  } 

  void onLoginReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onLoginReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    spdlog::info("in onLoginReceived");
    std::istream istrm(m_login_buf.get());
    std::getline(istrm, login);
    m_login_buf.reset(new asio::streambuf);

    spdlog::info("Login received: {}", login);
    bool login_is_correct = true;

    SACommand select_log(m_con.get(), "select login from User where login = :1");
    select_log << _TSA(login.c_str());
    select_log.Execute();
    bool login_in_database = select_log.FetchNext();

    if (sign_choice == "2") {
      if (login_in_database) {
        login_is_correct = false;
        asio::async_write(*m_sock.get(), asio::buffer("Login is taken. Enter unique login: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onLoginRequestSent(ec, bytes_transferred); 
          });
      }
    } 
    if (sign_choice == "1"){
      if (!login_in_database) {
        login_is_correct = false;
        asio::async_write(*m_sock.get(), asio::buffer("No such login registered. Enter login: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onLoginRequestSent(ec, bytes_transferred); 
          });
      } 
    }  

    if (login_is_correct) {
      asio::async_write(*m_sock.get(), asio::buffer("Enter password: \n"),
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onPasswordRequestSent(ec, bytes_transferred); 
        });
    }
  } 

  void onPasswordRequestSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onPasswordRequestSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    spdlog::info("Password request sent");
    asio::async_read_until(*m_sock.get(), *(m_password_buf.get()), '\n',
      [this](const system::error_code& ec, std::size_t bytes_transferred) {
        onPasswordReceived(ec, bytes_transferred);
      });
  }

  void onPasswordReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onPasswordReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 

    std::istream istrm(m_password_buf.get());
    std::getline(istrm, password);
    m_password_buf.reset(new asio::streambuf);

    spdlog::info("Password received: {}", password);
    
    SACommand select_log_pw(m_con.get(), "select login, password from User where login = :1 and password = :2");
    select_log_pw << _TSA(login.c_str()) << _TSA(password.c_str());
    select_log_pw.Execute();
    
    if (!select_log_pw.FetchNext()) {
      SACommand insert(m_con.get(), "insert into User (login, password) values (:1, :2)");
      insert << _TSA(login.c_str()) << _TSA(password.c_str());
      insert.Execute();
    } else {
      // TODO: 
    } 
    onFinish();
  } 

  void onFinish() {
    delete this;
  } 

private:
  std::shared_ptr<asio::ip::tcp::socket> m_sock;
  std::string m_response, login, password, sign_choice;
  std::shared_ptr<asio::streambuf> m_request, m_login_buf, m_password_buf, m_sign_choice;
  std::shared_ptr<SAConnection> m_con;
};


class Acceptor {
public:
  Acceptor(asio::io_context& ios, unsigned short port_num, std::shared_ptr<SAConnection> con):
    m_ios(ios),
    m_con(con),
    m_acceptor(m_ios, asio::ip::tcp::endpoint(asio::ip::address_v4::any(), port_num)),
    m_isStopped(false) {}

  void Start() {
    m_acceptor.listen();
    InitAccept();
  }

  void Stop() {
    m_isStopped.store(true);
  }

private:
  void InitAccept() {
    std::shared_ptr<asio::ip::tcp::socket> sock(new asio::ip::tcp::socket(m_ios));
    m_acceptor.async_accept(*sock.get(), [this, sock](const system::error_code& ec) {
          onAccept(ec, sock);
        });
  } 

  void onAccept(const system::error_code& ec, std::shared_ptr<asio::ip::tcp::socket> sock) {
    if (ec.value() == 0) {
      spdlog::info("Accepted connection from IP: {}", (*sock.get()).remote_endpoint().address().to_string());
      (new Service(sock, m_con))->StartHandling();
    } else {
      spdlog::error("Error in onAccept, code: {}, message: ", ec.value(), ec.message());
    }  

    if (!m_isStopped.load()) {
      InitAccept();
    } else {
      m_acceptor.close();
    } 
  } 

private:
  asio::io_context& m_ios;
  asio::ip::tcp::acceptor m_acceptor;
  std::atomic<bool> m_isStopped;
  std::shared_ptr<SAConnection> m_con;
}; 


class Server {
public:
  Server(std::string cfg_path) {
    std::ifstream cfg_istrm(cfg_path);
    json cfg = json::parse(cfg_istrm);

    std::string database_name = cfg["database_name"];
    std::string server_name = cfg["server_name"];
    std::string database_user = cfg["database_user"];
    std::string password = cfg["password"];
    
    std::string connection_string = server_name + "@" + database_name;

    std::cout << database_user << " " << password << std::endl;
    std::cout << connection_string << std::endl;
   
    con.reset(new SAConnection); 
    con->Connect(
        _TSA(connection_string.c_str()),
        _TSA(database_user.c_str()),
        _TSA(password.c_str()),
        SA_MySQL_Client);
    spdlog::info("Connected to database");
  }
  
  void Start(unsigned short port_num, unsigned int thread_pool_size) {
    assert(thread_pool_size > 0);
    acc.reset(new Acceptor(m_ios, port_num, con));
    acc->Start();

    for (int i = 0; i < thread_pool_size; i ++) {
      std::unique_ptr<std::thread> th(new std::thread(
            [this]() {
              m_ios.run();
            }));
      m_thread_pool.push_back(std::move(th));
    }   

  } 

  void Stop() {
    acc->Stop();
    m_ios.stop();

    for (auto& th: m_thread_pool) {
      th->join();
    } 
  } 

private:
  asio::io_context m_ios;
  std::unique_ptr<asio::io_context::work> m_work;
  std::unique_ptr<Acceptor> acc;
  std::vector<std::unique_ptr<std::thread>> m_thread_pool;
  std::shared_ptr<SAConnection> con;
};

const unsigned int DEFAULT_THREAD_POOL_SIZE = 2;

int main() {
  unsigned short port_num = 3333;

  spdlog::set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v"); 
  try {
    Server srv("/home/yen/Projects/mymsg/cfg_server.json");
    
    unsigned int thread_pool_size = std::thread::hardware_concurrency() * 2;
    if (thread_pool_size == 0) {
      thread_pool_size = DEFAULT_THREAD_POOL_SIZE;
    } 

    srv.Start(port_num, thread_pool_size);
    std::this_thread::sleep_for(std::chrono::seconds(50));
    srv.Stop();

  } 
  catch (system::system_error& e) {
    std::cout << e.code() << " " << e.what();
  } 
} 
