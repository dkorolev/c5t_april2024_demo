#pragma once

#include "typesystem/struct.h"

namespace current::htmlform {

CURRENT_STRUCT(FormResponse) {
  // What to `alert()`.
  CURRENT_FIELD(msg, Optional<std::string>);
  // What to append to the URL.
  CURRENT_FIELD(fwd, Optional<std::string>);

  FormResponse& Msg(std::string s) {
    msg = std::move(s);
    return *this;
  }
  FormResponse& Fwd(std::string s) {
    fwd = std::move(s);
    return *this;
  }
};

CURRENT_STRUCT(Field) {
  CURRENT_FIELD(id, std::string);
  CURRENT_FIELD(text, Optional<std::string>);
  CURRENT_FIELD(placeholder, Optional<std::string>);
  CURRENT_FIELD(value, Optional<std::string>);
  CURRENT_FIELD(type, Optional<std::string>);

  CURRENT_CONSTRUCTOR(Field)(std::string id = "id") : id(std::move(id)) {}

  Field& Text(std::string s) {
    text = std::move(s);
    return *this;
  }
  Field& Placeholder(std::string s) {
    placeholder = std::move(s);
    return *this;
  }
  Field& Value(std::string s) {
    value = std::move(s);
    return *this;
  }
  Field& Readonly() {
    type = "readonly";
    return *this;
  }
  Field& PasswordProtected() {
    type = "password";
    return *this;
  }
};

CURRENT_STRUCT(Form) {
  CURRENT_FIELD(title, std::string, "Current Form");
  CURRENT_FIELD(caption, Optional<std::string>);
  CURRENT_FIELD(fields, std::vector<Field>);
  CURRENT_FIELD(button_text, std::string, "Go");
  CURRENT_FIELD(on_submit, std::string, "input");

  Form& Add(Field f) {
    fields.push_back(std::move(f));
    return *this;
  }
  Form& Title(std::string s) {
    title = s;
    return *this;
  }
  Form& Caption(std::string s) {
    caption = s;
    return *this;
  }
  Form& ButtonText(std::string s) {
    button_text = s;
    return *this;
  }
  Form& OnSubmit(std::string s) {
    on_submit = s;
    return *this;
  }
};

std::string FormAsHTML(Form const&);

}  // namespace current::htmlform
