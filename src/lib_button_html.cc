#include "lib_button_html.h"  // IWYU pragma: keep

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
</html>
)";

std::string const& ButtonHtml() { return kHTML; }
