// ===========================================================================
// std/string.cpp - std::string implementation
// ===========================================================================

#include "string"
#include <string.h>  // strlen, strcmp, memcpy

namespace std {

void string::copy_from(const char* s, size_t n) {
    data_ = allocate<char>(n + 1);
    memcpy(data_, s, n);
    data_[n] = '\0';
    len_ = n;
}

// --- Constructors / Destructor ---

string::string() = default;

string::string(const char* s) {
    if (s) copy_from(s, strlen(s));
}

string::string(const char* s, size_t n) {
    if (s) copy_from(s, n);
}

string::string(const string& o) {
    if (o.data_) copy_from(o.data_, o.len_);
}

string::string(string&& o) noexcept
    : data_(o.data_), len_(o.len_)
{
    o.data_ = nullptr;
    o.len_  = 0;
}

string::~string() {
    if (data_) deallocate(data_);
}

// --- Assignment ---

string& string::operator=(const string& o) {
    if (this != &o) {
        string tmp(o);
        swap(*this, tmp);
    }
    return *this;
}

string& string::operator=(string&& o) noexcept {
    if (this != &o) {
        if (data_) deallocate(data_);
        data_   = o.data_;
        len_    = o.len_;
        o.data_ = nullptr;
        o.len_  = 0;
    }
    return *this;
}

string& string::operator=(const char* s) {
    string tmp(s);
    swap(*this, tmp);
    return *this;
}

// --- Access ---

const char* string::c_str()  const { return data_ ? data_ : ""; }
const char* string::data()   const { return data_ ? data_ : ""; }
size_t      string::size()   const { return len_; }
size_t      string::length() const { return len_; }
bool        string::empty()  const { return len_ == 0; }

char string::operator[](size_t i) const { return data_[i]; }

// --- Comparison ---

bool string::operator==(const string& o) const {
    if (len_ != o.len_) return false;
    if (len_ == 0)      return true;
    return strcmp(data_, o.data_) == 0;
}
bool string::operator!=(const string& o) const { return !(*this == o); }

bool string::operator==(const char* s) const {
    if (!s) return len_ == 0;
    return strcmp(c_str(), s) == 0;
}
bool string::operator!=(const char* s) const { return !(*this == s); }

// --- Swap ---

void swap(string& a, string& b) noexcept {
    std::swap(a.data_, b.data_);
    std::swap(a.len_,  b.len_);
}

} // namespace std
