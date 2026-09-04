#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "snapshot.hpp"

namespace shardgate {

enum class ErrorCode {
    None,
    AuthFailed,
    MalformedPayload,
    ShardWriteFailed,
    Unreachable,
    Unknown
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
};

inline int mongo_code(ErrorCode c) {
    switch (c) {
        case ErrorCode::AuthFailed:
            return 18;
        case ErrorCode::MalformedPayload:
            return 9;
        case ErrorCode::ShardWriteFailed:
            return 1;
        case ErrorCode::Unreachable:
            return 6;
        default:
            return 8;
    }
}

inline const char* mongo_code_name(ErrorCode c) {
    switch (c) {
        case ErrorCode::AuthFailed:
            return "AuthenticationFailed";
        case ErrorCode::MalformedPayload:
            return "FailedToParse";
        case ErrorCode::ShardWriteFailed:
            return "InternalError";
        case ErrorCode::Unreachable:
            return "HostUnreachable";
        default:
            return "UnknownError";
    }
}

inline ErrorCode error_code_from_mongo(long long code) {
    switch (code) {
        case 18:
            return ErrorCode::AuthFailed;
        case 9:
            return ErrorCode::MalformedPayload;
        case 1:
            return ErrorCode::ShardWriteFailed;
        case 6:
            return ErrorCode::Unreachable;
        default:
            return ErrorCode::Unknown;
    }
}

inline bool is_transport_failure(const Error& e) {
    return e.code == ErrorCode::Unreachable;
}

template <typename T>
class Result {
public:
    static Result success(T value) { return Result(std::move(value), {}); }

    static Result failure(Error error) {
        if (error.code == ErrorCode::None)
            throw std::invalid_argument("Failure result must carry an error");
        return Result(std::nullopt, std::move(error));
    }
    static Result failure(ErrorCode code, std::string message) {
        return failure(Error{code, std::move(message)});
    }

    bool is_success() const { return value_.has_value(); }
    bool is_failure() const { return !is_success(); }

    const T& value() const {
        if (is_failure())
            throw std::logic_error("Cannot access value of a failed result");
        return *value_;
    }

    const Error& error() const { return error_; }

    template <typename F>
    auto map(F&& f) const -> Result<std::invoke_result_t<F, const T&>> {
        using U = std::invoke_result_t<F, const T&>;
        return is_success() ? Result<U>::success(f(*value_))
                            : Result<U>::failure(error_);
    }

    template <typename F>
    auto and_then(F&& f) const -> std::invoke_result_t<F, const T&> {
        using R = std::invoke_result_t<F, const T&>;
        return is_success() ? f(*value_) : R::failure(error_);
    }

    template <typename OnSuccess, typename OnFailure>
    auto match(OnSuccess&& on_success, OnFailure&& on_failure) const {
        return is_success() ? on_success(*value_) : on_failure(error_);
    }

private:
    Result(std::optional<T> value, Error error)
        : value_(std::move(value)), error_(std::move(error)) {}

    std::optional<T> value_;
    Error error_;
};

struct Unit {};

namespace wire {

inline std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

inline std::string error_document(const Error& e) {
    return std::string("{\"success\":0,\"code\":") +
           std::to_string(mongo_code(e.code)) + ",\"codeName\":\"" +
           mongo_code_name(e.code) + "\",\"errmsg\":\"" + escape(e.message) +
           "\"}";
}

inline std::string success_document(const std::string& payload_fields = {}) {
    return payload_fields.empty() ? std::string("{\"success\":1}")
                                  : "{\"success\":1," + payload_fields + "}";
}

template <typename T, typename Deserialize>
Result<T> parse_reply(std::string_view line, Deserialize&& deserialize) {
    if (json::get_int(line, "success", -1) == 1)
        return Result<T>::success(deserialize(line));

    long long code = json::get_int(line, "code", 8);
    std::string errmsg = json::get_string(line, "errmsg");
    if (errmsg.empty()) errmsg = "malformed reply document";
    return Result<T>::failure(Error{error_code_from_mongo(code), errmsg});
}

}  // namespace wire
}  // namespace shardgate
