#include<boost/asio.hpp>
#include<nlohmann/json.hpp>
#include<spdlog/spdlog.h>
#include<spdlog/sinks/basic_file_sink.h>
#include<SQLAPI.h>


#include<iostream>
#include<fstream>

using json = nlohmann::json;
using namespace boost;

typedef void(*Callback) (unsigned int request_id, const std::string& response, const system::error_code& ec);


struct Session {
  Session(asio::io_context& ios, const std::string& raw_ip, unsigned short port_num,
     const std::string& request, unsigned int id, Callback callback):
    m_sock(ios),
    m_ep(asio::ip::address::from_string(raw_ip), port_num),
    m_request(request),
    m_id(id),
    m_callback(callback),
    m_was_cancelled(false) {
      m_sign_choice_buf.reset(new asio::streambuf);
      m_login_request_buf.reset(new asio::streambuf);
      m_login_validity_buf.reset(new asio::streambuf);
    }  

  asio::ip::tcp::socket m_sock;
  asio::ip::tcp::endpoint m_ep;
  std::string m_request;

  std::shared_ptr<asio::streambuf> m_login_request_buf, 
    m_login_validity_buf, m_sign_choice_buf;
  asio::streambuf m_password_request_buf;

  std::string m_response;
  system::error_code m_ec;

  unsigned int m_id;
  Callback m_callback;

  bool m_was_cancelled;
  std::mutex m_cancel_guard;
};

class AsyncTCPClient: public asio::noncopyable {
public:
  AsyncTCPClient() {
    initLogger(); 
    m_work.reset(new asio::io_context::work(m_ios));
    m_thread.reset(new std::thread(
          [this]() {
            m_ios.run();
          }));
  } 

  void initLogger() {
    try {
      logger = spdlog::basic_logger_mt("basig_logger", "logs/client_log.txt");
    }
    catch (const spdlog::spdlog_ex& ex) {
      logger = spdlog::default_logger();

      std::cout << "Log init failed: " << ex.what() << std::endl;
    }
    logger->set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v");
    logger->flush_on(spdlog::level::info);
  }

  void connect(const std::string& raw_ip, unsigned short port_num, Callback callback, unsigned int request_id ) {
    std::shared_ptr<Session> session(new Session(m_ios, raw_ip, port_num, "", request_id, callback));
    session->m_sock.open(session->m_ep.protocol());

    std::unique_lock<std::mutex> lock(m_active_sessions_guard);
    m_active_sessions[request_id] = session;
    lock.unlock();
  
    session->m_sock.async_connect(session->m_ep,
      [this, session](const system::error_code& ec) {
        onConnected(ec, session);
      });  
  }
     
  void onConnected(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
  asio::async_read_until(session->m_sock, *(session->m_sign_choice_buf.get()), '\n', 
    [this, session] (const system::error_code& ec, std::size_t) {
        onSignUpRequestReceived(ec, session);
      });
  }

  void onSignUpRequestReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    std::istream istrm(session->m_sign_choice_buf.get());
    std::string sign_choice_request;
    std::getline(istrm, sign_choice_request);
    session->m_sign_choice_buf.reset(new asio::streambuf);
    std::cout << sign_choice_request; 

    std::string sign_choice;
    std::cin >> sign_choice;

    asio::async_write(session->m_sock, asio::buffer(sign_choice + "\n"), 
      [this, session] (const system::error_code& ec, std::size_t) {
        onSignUpResponseSent(ec, session);
      }); 
    
  } 

  void onSignUpResponseSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onSignUpResponseSent");
    
    asio::async_read_until(session->m_sock, *(session->m_login_request_buf.get()), '\n', 
      [this, session] (const system::error_code& ec, std::size_t) {
        onLoginRequestReceived(ec, session);
      });
  }

  void onLoginRequestReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onLoginRequestReceived");
    std::istream istrm(session->m_login_request_buf.get());
    std::string response;

    std::getline(istrm, response);
    std::cout << response;
    session->m_login_request_buf.reset(new asio::streambuf);
      
    if (response == "Input incorrect. Enter 1 or 2: ") {
      std::string sign_choice;
      std::cin >> sign_choice;

      asio::async_write(session->m_sock, asio::buffer(sign_choice + "\n"), 
        [this, session] (const system::error_code& ec, std::size_t) {
          onSignUpResponseSent(ec, session);
        }); 
    } else {
      std::string login;
      std::cin >> login;

      asio::async_write(session->m_sock, asio::buffer(login + "\n"), 
        [this, session] (const system::error_code& ec, std::size_t) {
          onLoginResponseSent(ec, session);
        }); 
    }
  } 
  
  void onLoginResponseSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onLoginResponseSent");

    asio::async_read_until(session->m_sock, *(session->m_login_validity_buf.get()), '\n', 
      [this, session] (const system::error_code& ec, std::size_t) {
        onLoginValidityResponseReceived(ec, session);
      });
  } 

  void onLoginValidityResponseReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onLoginValidityResponseReceived");
    
    std::istream istrm(session->m_login_validity_buf.get());
    std::string login_validity_response;
    std::getline(istrm, login_validity_response);
    std::cout << login_validity_response;
    session->m_login_validity_buf.reset(new asio::streambuf);
        
    if (login_validity_response == "Login is taken. Enter unique login: " ||
        login_validity_response == "No such login registered. Enter login: ") {
      std::string login;
      std::cin >> login;

      asio::async_write(session->m_sock, asio::buffer(login + "\n"), 
        [this, session] (const system::error_code& ec, std::size_t) {
          onLoginResponseSent(ec, session);
        }); 
    } 
    if (login_validity_response == "Enter password: ") {
      onPasswordRequestReceived(ec, session);
    } 
  } 

  void onPasswordRequestReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }

    std::istream istrm(&session->m_password_request_buf);
    std::string response;

    std::getline(istrm, response);
    std::cout << response;

    std::string password;
    std::cin >> password;

    asio::async_write(session->m_sock, asio::buffer(password + "\n"),
      [this, session] (const system::error_code& ec, std::size_t) {
        onPasswordSent(ec, session);
      });
  } 

  void onPasswordSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
    } else {
    } 
    onRequestComplete(session);

  } 

  void cancelRequest(unsigned int request_id) {
    std::unique_lock<std::mutex> lock(m_active_sessions_guard);

    auto it = m_active_sessions.find(request_id);
    if (it != m_active_sessions.end()) {
      std::unique_lock<std::mutex> cancel_lock(it->second->m_cancel_guard);
      it->second->m_sock.cancel();
    } 
  } 

  void close() {
    m_work.reset(NULL);
    m_thread->join();
  } 

private:
  void onRequestComplete(std::shared_ptr<Session> session) {
    logger->info("Completing request");
    system::error_code ignored_ec;

    session->m_sock.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);

    std::unique_lock<std::mutex> lock(m_active_sessions_guard);
    auto it = m_active_sessions.find(session->m_id);
    if (it != m_active_sessions.end()) {
      m_active_sessions.erase(it);
    } 
    lock.unlock();

    system::error_code ec;
    if (session->m_ec.value() == 0 && session->m_was_cancelled) {
      ec = asio::error::operation_aborted; 
    } else {
      ec = session->m_ec;
    } 

    session->m_callback(session->m_id, session->m_response, ec);
  } 

private:
  asio::io_context m_ios;
  std::map<int, std::shared_ptr<Session>> m_active_sessions;
  std::mutex m_active_sessions_guard;
  std::unique_ptr<asio::io_context::work> m_work;
  std::unique_ptr<std::thread> m_thread;
  std::shared_ptr<spdlog::logger> logger;
};

void handler(unsigned int request_id, const std::string& response, const system::error_code& ec) {

  /*  
  if (ec.value() == 0) {
    std::cout << "Request #" << request_id << " has completed. Response: " << response << std::endl;
  } else {
    std::cout << "Request #" << request_id << " failed. Error code: "
      << ec.value() << " " << ec.message() << std::endl;
  } 
  */
} 


int main() {
  std::ifstream cfg_istrm("/home/yen/Projects/mymsg/cfg_client.json");
  json cfg = json::parse(cfg_istrm);

  std::string raw_ip = cfg["server_ip"];
  unsigned short port_num = cfg["server_port"];

  try {
    AsyncTCPClient client;

    client.connect(raw_ip, port_num, handler, 1);
    //client.emulate(10, raw_ip, port_num, handler, 1);
    std::this_thread::sleep_for(std::chrono::seconds(50));
    client.close();
  } 
  catch (system::system_error& e) {
    std::cout << e.code() << " " << e.what();
  } 
} 
