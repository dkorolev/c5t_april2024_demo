#include "lib_button.h"
#include "lib_button_html.h"
#include "blocks/http/api.h"
#include "typesystem/struct.h"

CURRENT_STRUCT(SumResponse) { CURRENT_FIELD(sum, int64_t); };

void ServeButton(Request req) {
  using namespace current::htmlform;
  if (req.method == "GET") {
    auto const f = Form()
                       .Add(Field("a").Text("First summand").Placeholder("3 for example"))   //.Value("3"))
                       .Add(Field("b").Text("Second summand").Placeholder("4 for example"))  // .Value("4"))
                       .Title("Current Sum")
                       .Caption("Sum")
                       .ButtonText("Add")
                       .OnSubmit(R"({
        const a = parseInt(input.a);
        if (isNaN(a)) return { error: "A is not a number." };
        const b = parseInt(input.b);
        if (isNaN(b)) return { error: "B is not a number." };
        return { sum: a + b };
      })");
    req(FormAsHTML(f), HTTPResponseCode.OK, current::net::constants::kDefaultHTMLContentType);
  } else {
    FormResponse res;
    try {
      auto const body = ParseJSON<SumResponse>(req.body);
      res.fwd = "/sum/" + current::ToString(body.sum);
    } catch (current::Exception& e) {
      res.msg = "error!";
    }
    req(res);
  }
}
