#include "AccountMenuForm.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        using namespace AccountMenu;
        Form form;
        std::string error;
        int requests = 0;
        Login login = [&](const std::string& email, const std::string& password, std::string&) {
            ++requests;
            return email == "player@example.com" && password == " secret pass ";
        };
        require(!form.submit(login, error) && requests == 0 && !error.empty(), "Empty credentials must not send a request");
        form.editing = Field::Email;
        form.insert(" player@example.com ");
        form.editing = Field::Password;
        form.insert(" secret pass ");
        const auto masked = form.display(Field::Password);
        require(masked.find("secret") == std::string::npos && masked.find('*') != std::string::npos,
            "Password labels must only show masking characters");
        require(form.submit(login, error) && requests == 1 && error.empty(), "Submit must trim email and preserve password spaces");
        require(form.password.value.empty() && form.editing == Field::None, "Success must clear the password and leave editing mode");
        form.editing = Field::Password;
        form.insert("wrong-password");
        require(!form.submit([](const auto&, const auto&, std::string& message) { message = "Invalid email or password."; return false; }, error),
            "Failed authentication must remain failed");
        require(error == "Invalid email or password." && form.password.value == "wrong-password",
            "Failed authentication must show the server error and allow correction");
        require(!form.submit({}, error) && !error.empty(), "Missing login support must report an error");

        TextEntry text;
        text.insert("a\xc3\xa9z", 254);
        text.move(-1);
        text.backspace();
        require(text.value == "az" && text.cursor == 1, "Backspace must remove an entire UTF-8 character");
        text.insert("XY", 254);
        text.move(-1);
        text.eraseForward();
        require(text.value == "aXz", "Insertion and forward delete must use the cursor position");
        text.selected = true;
        text.insert("replacement", 254);
        require(text.value == "replacement", "Select-all typing must replace the field");
        text.selected = true;
        text.backspace();
        require(text.value.empty() && text.cursor == 0, "Select-all deletion must clear the field");
        text.insert(std::string(253, 'a') + "\xc3\xa9", 254);
        require(text.value.size() == 253, "Length limits must not split a UTF-8 character");
        form.password.clear();
        form.insert(std::string(300, 'x'));
        require(form.password.value.size() == 256, "Password input must be bounded");
        form.email.clear();
        form.editing = Field::Email;
        form.insert("name\r\n@example.com");
        require(form.email.value == "name@example.com", "Single-line input must reject line breaks");
        std::cout << "Account menu form tests passed\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
