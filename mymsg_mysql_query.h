#ifndef MYMSG_MYSQL_QUERY
#define MYMSG_MYSQL_QUERY

#include<boost/asio.hpp>
#include<mymsg_tools.h>
#include<SQLAPI.h>
#include<spdlog/spdlog.h>

#include<iostream>

namespace DBquery {
  MySQLQueryState is_login_found(std::shared_ptr<SAConnection> con, const std::string& login) {
    MySQLQueryState res = MYSQL_ERROR;
    try {
      SACommand select_log(con.get(), "select login from User where login = :1");
      select_log << _TSA(login.c_str());
      select_log.Execute();
      if(select_log.FetchNext()) {
        res = FOUND;
      } else {
        res = NOT_FOUND;
      } 
    } catch (SAException& ex) {
      spdlog::error(ex.ErrText().GetMultiByteChars());
    } 
    return res;
  } 

  bool are_credentials_registred(std::shared_ptr<SAConnection> con, const std::string& login,
      const std::string& password) {
    SACommand select_log_pw(con.get(), "select login, password from User where login = :1 and password = :2");
    select_log_pw << _TSA(login.c_str()) << _TSA(password.c_str());
    select_log_pw.Execute();
    return select_log_pw.FetchNext();
  } 

  void register_user(std::shared_ptr<SAConnection> con, const std::string& login, const std::string& password) {
      SACommand insert(con.get(), "insert into User (login, password) values (:1, :2)");
      insert << _TSA(login.c_str()) << _TSA(password.c_str());
      insert.Execute();
  } 


  long get_user_id(std::shared_ptr<SAConnection> con, const std::string& user_login) {
    spdlog::info("in _get_user_id, login: {}", user_login);
    SACommand select_user_id(con.get(), "select user_id from User where login = :1");
    select_user_id << _TSA(user_login.c_str());
    select_user_id.Execute();
    select_user_id.FetchNext();
    return select_user_id.Field("user_id").asLong();
  } 
  
  std::string get_login(std::shared_ptr<SAConnection> con, const long user_id) {
    spdlog::info("in _get_login");
    SACommand select_user_id(con.get(), "select login from User where user_id = :1");
    select_user_id << user_id;
    select_user_id.Execute();
    select_user_id.FetchNext();
    std::string res = select_user_id.Field("login").asString().GetMultiByteChars();
    return res;
  } 

  std::vector<long> get_dialogs_id_list(std::shared_ptr<SAConnection> con, const std::string& user_login) {
    spdlog::info("in _get_dialogs_id_list");
    long user_id = get_user_id(con, user_login);
    SACommand select_dialog_id(con.get(), "select dialog_id from `User/Dialog` where user_id = :1");
    select_dialog_id << user_id;
    select_dialog_id.Execute();
    std::vector<long> dialogs_id_list;
    while(select_dialog_id.FetchNext()) {
      dialogs_id_list.push_back(select_dialog_id.Field("dialog_id").asLong());
    }
    return dialogs_id_list;
  } 

  std::string get_another_party_login(std::shared_ptr<SAConnection> con, const long dialog_id,
      const std::string& login) {
    spdlog::info("in _get_another_party_login");
    std::string login1, login2;
    
    SACommand select_logins_id(con.get(),
        "select login1_id, login2_id from Dialog where dialog_id = :1");
    select_logins_id << dialog_id;
    select_logins_id.Execute();
    select_logins_id.FetchNext();
    
    long id_1 = select_logins_id.Field("login1_id").asLong();
    long id_2 = select_logins_id.Field("login2_id").asLong();
    select_logins_id.Close();

    login1 = get_login(con, id_1);
    login2 = get_login(con, id_2);

    if (login1 == login) {
      return login2;
    } 
    return login1;
  } 

  std::vector<std::tuple<long, long, std::string, std::string>> get_messages(std::shared_ptr<SAConnection> con, 
      const long id1, const long id2) {
    std::vector<std::tuple<long, long, std::string, std::string>> res;
    try {
      spdlog::info("in _get_messages_ids");
      SACommand select_messages_ids(con.get(),
          "select login_sender_id, login_recipient_id, date, content"
          "from Message where (login_sender_id = :1 and login_recipient_id = :2) or " 
          "(login_sender_id = :2 and login_recipient_id = :1) order by date");
      std::cout << id1 << " " << id2 << std::endl;
      select_messages_ids << id1 << id2;
      select_messages_ids.Execute();

      while (select_messages_ids.FetchNext()) {
        long id = select_messages_ids.Field("message_id").asLong();
      } 
    } catch (SAException &x) {
      spdlog::error(x.ErrText().GetMultiByteChars());
    } 
    return res;
  } 

  std::string get_dialogs_list(std::shared_ptr<SAConnection> con, const std::string& login) { 
    spdlog::info("in _get_dialogs_list");
    std::vector<long> dialogs_id_list = get_dialogs_id_list(con, login);
    std::string res = "";
    for (int i = 0; i < dialogs_id_list.size(); i ++) {
      res += get_another_party_login(con, dialogs_id_list[i], login) + " "; 
    } 
    return res;
  } 

  void insert_message(std::shared_ptr<SAConnection> con, const long login_sender_id,
      const long login_recipient_id, const std::string& content, const long dialog_id) {
    spdlog::info("in _insert_message, content: '{}'", content);

    try {
      SACommand insert_message(con.get(), 
          "insert into Message(login_sender_id, login_recipient_id, date, content, dialog_id) values "
          "(:1, :2, :3, :4, :5)");

      std::string date = _get_date_str();
      insert_message << login_sender_id << login_recipient_id
        << _TSA(date.c_str()) << _TSA(content.c_str()) << dialog_id;
      insert_message.Execute();
    } catch (SAException &x) {
      spdlog::error(x.ErrText().GetMultiByteChars());
    }
  } 
}

#endif
