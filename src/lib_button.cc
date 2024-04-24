#include "lib_button.h"
#include "lib_button_html.h"
#include "blocks/http/api.h"

void ServeButton(Request r) {
  if (r.method == "GET") {
    r(ButtonHtml(), HTTPResponseCode.OK, current::net::constants::kDefaultHTMLContentType);
  } else {
    r("[1,2,3,42,100]");
  }
}
