#include "../../jvpatterns.hpp"

using jvpatterns::until;
using jvpatterns::operator""_p;

// Tags
struct TagMajor; struct TagMinor;
struct TagMethod; struct TagTarget; struct TagRequest;
struct TagStatusCode; struct TagStatusMessage; struct TagResponse;
struct TagHeaderName; struct TagHeaderValue; struct TagHeader;
struct TagBody;

// This will match an individual upper letter.
auto upperLetter = jvpatterns::predicate_elem{ [](auto elem) {
	return elem >= 'A' && elem <= 'Z';
} };
// This will match an individual digit.
auto digit = jvpatterns::any_of<std::string_view>{ "0123456789" };

auto httpVersion = "HTTP/"_p + digit.tag<TagMajor*>() + "."_p + digit.tag<TagMinor*>();
auto requestLine = (upperLetter.repeat(1, 100).tag<TagMethod*>() + " "_p
	+ until{ " "_p }.tag<TagTarget*>() + " "_p + httpVersion + "\r\n"_p
	).tag<TagRequest*>();
auto responseLine = (httpVersion + " "_p + digit[3].tag<TagStatusCode*>() + " "_p
	+ until{ "\r\n"_p }.tag<TagStatusMessage*>() + "\r\n"_p
	).tag<TagResponse*>();
auto startLine = requestLine | responseLine;

auto headerName = until{ ":"_p }.tag<TagHeaderName*>();
auto headerValue = until{ "\r\n"_p }.tag<TagHeaderValue*>();
auto header = (headerName + ":"_p + " "_p.repeat(0, 100) + headerValue + "\r\n"_p).tag<TagHeader*>();

// This will match the the rest of the message, which is the body.
auto body =
	jvpatterns::predicate{ [](auto it, auto end) { return std::optional{end}; } }.tag<TagBody*>();

auto httpMessage = startLine + header.repeat(0, 100) + "\r\n"_p + body;

#include <map>
#include <variant>
#include <string>

struct Request { std::string method, target; };
struct Response { std::string status_code, status_message; };

struct HttpVisitor {
	int major = 0, minor = 0;
	std::variant<Request, Response> info;
	std::map<std::string, std::string> headers;
	std::string body;

	template<typename Pattern, typename FwdIt>
	void operator()(Pattern const& p, FwdIt begin, FwdIt end);

	// used for operator(), explained later
	std::string _tmp_header_name;
	std::string _tmp_header_value;
};

// For TagMethod and TagTarget, we must ensure that visitor.info holds the alternative Request.
// The same applies for TagStatusCode, TagStatusMessage and Response.
// This is a precondition (before TagRequest and TagResponse being actually matched).
// So it requires to use ADL technique by overloading match(...).
// We want to only overload match(...) for TagRequest and TagResponse:

template<typename P, typename FwdIt>
std::optional<FwdIt> match(jvpatterns::tagged_pattern<P, TagRequest*> const& pattern,
	FwdIt begin, FwdIt end, HttpVisitor& visitor) {
	// only for TagRequest
	visitor.info = Request{};
	return pattern(begin, end, visitor);
}
template<typename P, typename FwdIt>
std::optional<FwdIt> match(jvpatterns::tagged_pattern<P, TagResponse*> const& pattern,
	FwdIt begin, FwdIt end, HttpVisitor& visitor) {
	// only for TagResponse
	visitor.info = Response{};
	return pattern(begin, end, visitor);
}

// Other tagged patterns only need to save their matches, so we can do it with operator().
template<typename Pattern, typename FwdIt>
void HttpVisitor::operator()(Pattern const& p, FwdIt begin, FwdIt end) {
	if constexpr (std::is_same_v<typename Pattern::tag_type, TagMajor*>)
		major = *begin - '0';
	if constexpr (std::is_same_v<typename Pattern::tag_type, TagMinor*>)
		minor = *begin - '0';

	if constexpr (std::is_same_v<typename Pattern::tag_type, TagMethod*>)
		std::get<Request>(info).method = std::string{ begin, end };
	if constexpr (std::is_same_v<typename Pattern::tag_type, TagTarget*>)
		std::get<Request>(info).target = std::string{ begin, end };

	if constexpr (std::is_same_v<typename Pattern::tag_type, TagStatusCode*>)
		std::get<Response>(info).status_code = std::string{ begin, end };
	if constexpr (std::is_same_v<typename Pattern::tag_type, TagStatusMessage*>)
		std::get<Response>(info).status_message = std::string{ begin, end };

	if constexpr (std::is_same_v<typename Pattern::tag_type, TagHeaderName*>)
		_tmp_header_name = std::string{ begin, end };
	if constexpr (std::is_same_v<typename Pattern::tag_type, TagHeaderValue*>)
		_tmp_header_value = std::string{ begin, end };
	if constexpr (std::is_same_v<typename Pattern::tag_type, TagHeader*>)
		headers.emplace(std::move(_tmp_header_name), std::move(_tmp_header_value));

	if constexpr (std::is_same_v<typename Pattern::tag_type, TagBody*>)
		body = std::string{ begin,end };
}

#include <iostream>

void parse_and_print(std::string_view http) {
	HttpVisitor visitor;
	auto result = match(httpMessage, http.begin(), http.end(), visitor);

	std::cout << "====================================================================\n";
	std::cout << http;
	std::cout << "===============\n";
	std::cout << "Has matched? " << (result ? "YES" : "NO") << "\n";
	if (result) {
		std::cout << "HTTP Version: " << visitor.major << '.' << visitor.minor << '\n';
		if (Response const* response = std::get_if<Response>(&visitor.info)) {
			std::cout << "Type: Response\n";
			std::cout << "Code: " << response->status_code << "\n";
			std::cout << "Message: " << response->status_message << "\n";
		}
		else {
			Request const& request = std::get<Request>(visitor.info);
			std::cout << "Type: Request\n";
			std::cout << "Method: " << request.method << "\n";
			std::cout << "Target: " << request.target << "\n";
		}
		std::cout << "Headers: \n";
		for (auto [name, value] : visitor.headers)
			std::cout << "- " << name << " = " << value << "\n";
		std::cout << "Body: " << visitor.body << "\n";
	}
}


int main() {

	parse_and_print(
		"GET /hello.html HTTP/1.1\r\n"
		"Host: localhost:8000\r\n"
		"Connection: keep-alive\r\n"
		"Accept: text/html\r\n"
		"\r\n");

	parse_and_print(
		"HTTP/1.1 200 OK\r\n"
		"Connection: Keep-Alive\r\n"
		"Content-Type:text/html\r\n"
		"Content-Length:      22\r\n"
		"\r\n"
		"<h1>Hello World!</h1>\n");

	parse_and_print(
		"GET / HTTP/1.0\r\n"
		"\r\n");

	parse_and_print(
		"HTTP/1.0 2000 Invalid Status - Error intended\r\n"
		"\r\n");
}




