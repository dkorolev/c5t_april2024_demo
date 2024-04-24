#include "lib_button.h"
#include "blocks/http/api.h"

const std::string kHTML = R"(<!DOCTYPE HTML>
<html>
  <head><title>current button demo</title></head>
  <body>
    <h1>I am a WIP button.</h1>
    <script>
      (async () => {
        const body = "blah";
        const resp = await fetch(window.location.href, { method: "POST", body });
        const json = await resp.json();
        console.log(json);
      })();
    </script>
  </body>
</html>)";

void ServeButton(Request r) {
  if (r.method == "GET") {
    r(kHTML, HTTPResponseCode.OK, current::net::constants::kDefaultHTMLContentType);
  } else {
    r("[1,2,3,42,100]");
  }
}
