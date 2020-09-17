#pragma once

#include <memory>

namespace gnat {

class Status {
public:
    enum class Code {
        OK,
	FAILURE,
    };

    static Status Ok() {
    	return Status(Code::OK, nullptr);
    }

    //TODO string_view when I can get arduino to support it.
    static Status Failure(std::string message) {
    	return Status(Code::FAILURE, std::make_unique<std::string>(message.data(), message.length()));
    }

    Status(Code code, std::unique_ptr<std::string> message) 
	    : code_(code), message_(std::move(message)) {}

    const std::string& message() const {
        if (!message_) return kEmpty;

    	return *message_;
    }

    bool IsOk() const {
        return code_ == Code::OK;
    }

    bool operator==(const Status& other) const {
        if (code_ != other.code_) return false;
        return message() == other.message();
    }

private:
    Code code_;
    std::unique_ptr<std::string> message_;
    const static std::string kEmpty;
};

const std::string Status::kEmpty("");

} // namespace gnat
