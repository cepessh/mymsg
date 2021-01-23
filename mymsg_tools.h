#ifndef MYMSG_TOOLS
#define MYMSG_TOOLS

#include<ctime>
#include<iostream>
#include<iomanip>
#include<sstream>


enum SignChoice {
  SIGN_IN = 1,
  SIGN_UP
};

enum ActionChoice {
  LIST_DIALOGS = 1,
  ADD_CONTACT
};

enum MySQLQueryState {
  MYSQL_ERROR = -1,
  NOT_FOUND,
  FOUND
};

enum LoginValidity {
  IS_TAKEN,
  NOT_REGISTERED,
  IS_VALID
};

enum Authorization {
  AUTHORIZED,
  WRONG_LOG_PASS
};

std::string _get_date_str() {
  auto t = std::time(nullptr);
  auto tm = *std::localtime(&t);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  std::string date = oss.str();
  return date;
} 

std::string _form_message_str(const std::string& date, const std::string& login_sender, const std::string& content) {
  std::string res = "";
  res.append("[");
  res.append(date);
  res.append("] [");
  res.append(login_sender);
  res.append("] ");
  res.append(content);
  res.append("\n");
  return res;
} 

std::string _form_message_str(const std::string& login_sender, const std::string& content) {
  std::string res = "";
  std::string date = _get_date_str();
  return _form_message_str(date, login_sender, content);
} 

#endif
