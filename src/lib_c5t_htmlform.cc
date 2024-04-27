#include "lib_c5t_htmlform.h"  // IWYU pragma: keep

#include <sstream>

std::string current::htmlform::FormAsHTML(Form const& form) {
  std::ostringstream oss;
  // NOTE(dkorolev): This _could_ be created using client-side JavaScript too.
  oss << R"(<!DOCTYPE HTML>
<html>
  <head><title>)"
      << form.title + R"(</title></head>
  <body>)";
  if (Exists(form.caption)) {
    oss << R"(
    <h2>)"
        << Value(form.caption) << "</h2>";
  }
  oss << R"(
    <form id="form">)";
  for (auto const& f : form.fields) {
    oss << R"(
      <p>)"
        << (Exists(f.text) ? Value(f.text) : f.id) << R"(</p>
      <input
        type="text"
        autocomplete=false
        id=")"
        << f.id << '"';
    if (Exists(f.placeholder)) {
      oss << R"(
        placeholder=")"
          << Value(f.placeholder) << '"';
    }
    if (Exists(f.value)) {
      oss << R"(
        value=")"
          << Value(f.value) << '"';
    }
    oss << R"(
        size="100"
      />
      <br>)";
  }
  oss << R"(
      <br>
      <button type="submit">)"
      << form.button_text << R"(</button>
    </form>
    <script>
      const transform = (input) => )"
      << form.on_submit << R"(;
      const e_form = document.getElementById("form");
      const e_fields = {)";
  for (auto const& f : form.fields) {
    oss << R"(
        )"
        << f.id << R"(: document.getElementById(")" << f.id << "\"),";
  }
  oss << R"(
      };
      e_form.addEventListener("submit", (e) => {
        e.preventDefault();
        let input = {};
        for (const f in e_fields) {
          input[f] = e_fields[f].value;
        }
        const sent = transform(input);
        if (sent.error) {
          alert(sent.error);
        } else {
          (async () => {
            const body = JSON.stringify(sent);
            fetch(window.location.href, { method: "POST", body }).then(r => r.json()).then(
             j => {
               if (j.msg) {
                 alert(j.msg);
               }
               if (j.fwd) {
                 let u = window.location.href;
                 if (u[u.length - 1] != '/') {
                   u += '/';
                 }
                 if (j.fwd[0] == '/') {
                   u += j.fwd.substr(1);
                 } else {
                   u += j.fwd;
                 }
                 window.location.href = u;
               }
             }
            ).catch(e => {});
          })();
        }
      });
    </script>
  </body>
</html>
)";
  return oss.str();
}
