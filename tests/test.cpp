#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFile(const fs::path &path, const std::string &content) {
    std::ofstream out(path);
    out << content;
}

std::string runRefactor(const std::string &code) {
    auto tmp = fs::temp_directory_path() / "refactor_test.cpp";
    writeFile(tmp, code);
    std::string cmd = std::string(TOOL_PATH) + " " + tmp.string() + " -- -std=c++17 2>/dev/null";
    std::system(cmd.c_str());
    auto result = readFile(tmp);
    fs::remove(tmp);
    return result;
}

}  // namespace

// ===== Virtual Destructor =====

TEST(VirtualDtor, AddsVirtualToBaseWithDerived) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("virtual ~Base()"), std::string::npos);
}

TEST(VirtualDtor, DoesNotModifyAlreadyVirtual) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result, input);
}

TEST(VirtualDtor, DoesNotModifyStandaloneClass) {
    std::string input = "class Standalone {\n"
                        "public:\n"
                        "    ~Standalone() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result, input);
}

// ===== Override =====

TEST(Override, AddsOverrideToOverridingMethod) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void foo() {}\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void foo() override {}"), std::string::npos);
}

TEST(Override, DoesNotModifyAlreadyMarked) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void foo() {}\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() override {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result, input);
}

TEST(Override, DoesNotAddOverrideToDestructor) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    ~Derived() {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result.find("~Derived() override"), std::string::npos);
}

TEST(Override, HandlesConstQualifier) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void foo() const {}\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() const {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void foo() const override {}"), std::string::npos);
}

TEST(Override, HandlesNoexceptAndRefQualifier) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void foo() const noexcept {}\n"
                        "    virtual void bar() && {}\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() const noexcept {}\n"
                        "    void bar() && {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void foo() const noexcept override {}"), std::string::npos);
    EXPECT_NE(result.find("void bar() && override {}"), std::string::npos);
}

TEST(Override, HandlesBlockComment) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void foo() {}\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() /* comment */ {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void foo() /* comment */ override {}"), std::string::npos);
}

TEST(Override, HandlesNoexceptExpr) {
    std::string input = "class Base {\n"
                        "public:\n"
                        "    virtual void foo() noexcept(true) {}\n"
                        "    virtual ~Base() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() noexcept(true) {}\n"
                        "};\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("void foo() noexcept(true) override {}"), std::string::npos);
}

// ===== Range-for =====

TEST(RangeFor, AddsRefToConstAuto) {
    std::string input = "#include <vector>\n"
                        "#include <string>\n"
                        "struct S { int x; std::string y; };\n"
                        "void f() {\n"
                        "    std::vector<S> v;\n"
                        "    for (const auto x : v) {}\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("const auto& x"), std::string::npos);
}

TEST(RangeFor, DoesNotModifyFundamentalType) {
    std::string input = "#include <vector>\n"
                        "void f() {\n"
                        "    std::vector<int> v;\n"
                        "    for (const int x : v) {}\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_NE(result.find("const int x"), std::string::npos);
    EXPECT_EQ(result.find("const int& x"), std::string::npos);
}

TEST(RangeFor, DoesNotModifyAlreadyRef) {
    std::string input = "#include <vector>\n"
                        "#include <string>\n"
                        "struct S { int x; std::string y; };\n"
                        "void f() {\n"
                        "    std::vector<S> v;\n"
                        "    for (const auto& x : v) {}\n"
                        "}\n";

    std::string result = runRefactor(input);
    EXPECT_EQ(result, input);
}
