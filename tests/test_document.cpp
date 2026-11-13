#include <doctest.h>

#include "savetinker/document.hpp"
#include "test_support.hpp"

using st::Bytes;
using st::Document;

namespace {

Document makeDocument() {
    Document document;
    document.addFile("save.dat", Bytes{0, 0, 0, 0, 0, 0, 0, 0});
    return document;
}

}

TEST_CASE("single writes become undoable steps") {
    auto document = makeDocument();
    document.writeValue<std::uint32_t>("save.dat", 0, 100);
    document.writeValue<std::uint32_t>("save.dat", 4, 7);
    CHECK(document.history().size() == 2);

    REQUIRE(document.undo());
    CHECK(document.file("save.dat").read<std::uint32_t>(4) == 0);
    CHECK(document.file("save.dat").read<std::uint32_t>(0) == 100);
    REQUIRE(document.redo());
    CHECK(document.file("save.dat").read<std::uint32_t>(4) == 7);
    CHECK_FALSE(document.redo());
}

TEST_CASE("writing the same value records nothing") {
    auto document = makeDocument();
    document.writeValue<std::uint8_t>("save.dat", 0, 0);
    CHECK_FALSE(document.canUndo());
}

TEST_CASE("transactions group writes into one step") {
    auto document = makeDocument();
    document.beginTransaction("Enable all");
    for (std::size_t i = 0; i < 8; ++i) document.writeValue<std::uint8_t>("save.dat", i, 1);
    document.commitTransaction();

    CHECK(document.history().size() == 1);
    CHECK(document.undoLabel() == "Enable all");
    document.undo();
    CHECK(document.file("save.dat").bytes() == Bytes(8, 0));
}

TEST_CASE("nested transactions commit once") {
    auto document = makeDocument();
    document.beginTransaction("outer");
    document.beginTransaction("inner");
    document.writeValue<std::uint8_t>("save.dat", 0, 1);
    document.commitTransaction();
    document.writeValue<std::uint8_t>("save.dat", 1, 1);
    document.commitTransaction();
    CHECK(document.history().size() == 1);
    CHECK(document.history()[0].label == "outer");
}

TEST_CASE("repeated writes to one spot merge inside a transaction") {
    auto document = makeDocument();
    document.beginTransaction("Slide");
    for (std::uint32_t value = 1; value <= 50; ++value) document.writeValue("save.dat", 0, value);
    document.commitTransaction();
    CHECK(document.history()[0].patches.size() == 1);
    document.undo();
    CHECK(document.file("save.dat").read<std::uint32_t>(0) == 0);
}

TEST_CASE("rollback restores bytes and leaves no history") {
    auto document = makeDocument();
    {
        st::TransactionScope scope(document, "abandoned");
        document.writeValue<std::uint16_t>("save.dat", 2, 0xFFFF);
    }
    CHECK(document.file("save.dat").bytes() == Bytes(8, 0));
    CHECK_FALSE(document.canUndo());
}

TEST_CASE("a new edit after undo drops the redo branch") {
    auto document = makeDocument();
    document.writeValue<std::uint8_t>("save.dat", 0, 1);
    document.writeValue<std::uint8_t>("save.dat", 1, 1);
    document.undo();
    document.writeValue<std::uint8_t>("save.dat", 2, 1);
    CHECK_FALSE(document.canRedo());
    CHECK(document.history().size() == 2);
}

TEST_CASE("splice changes size and undoes cleanly") {
    Document document;
    document.addFile("text.json", test::bytesOf("{\"a\":1}"));
    document.splice("text.json", 5, 1, test::viewOf("1000"));
    CHECK(document.file("text.json").bytes() == test::bytesOf("{\"a\":1000}"));
    document.setContent("text.json", test::bytesOf("{}"));
    document.undo();
    document.undo();
    CHECK(document.file("text.json").bytes() == test::bytesOf("{\"a\":1}"));
}

TEST_CASE("dirty state follows the saved point") {
    auto document = makeDocument();
    CHECK_FALSE(document.isDirty());
    document.writeValue<std::uint8_t>("save.dat", 0, 1);
    CHECK(document.isDirty());
    document.markSaved();
    CHECK_FALSE(document.isDirty());
    document.undo();
    CHECK(document.isDirty());
    document.redo();
    CHECK_FALSE(document.isDirty());
    document.undo();
    document.writeValue<std::uint8_t>("save.dat", 0, 2);
    CHECK(document.isDirty());
}

TEST_CASE("history limit drops oldest steps") {
    Document document(3);
    document.addFile("f", Bytes(8));
    for (std::uint8_t i = 0; i < 5; ++i) document.writeValue<std::uint8_t>("f", i, 1);
    CHECK(document.history().size() == 3);
    while (document.undo()) {}
    CHECK(document.file("f").bytes() == Bytes{1, 1, 0, 0, 0, 0, 0, 0});
    CHECK(document.isDirty());
}

TEST_CASE("changes since load lists modified ranges") {
    auto document = makeDocument();
    document.writeValue<std::uint16_t>("save.dat", 1, 0xFFFF);
    document.writeValue<std::uint8_t>("save.dat", 6, 9);
    const auto changes = document.changesSinceLoad();
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].offset == 1);
    CHECK(changes[0].length == 2);
    CHECK(changes[1].offset == 6);
    CHECK(changes[1].length == 1);
}

TEST_CASE("listeners hear about writes, undo and redo") {
    auto document = makeDocument();
    int calls = 0;
    const int id = document.addListener([&](const std::string& file) {
        CHECK(file == "save.dat");
        ++calls;
    });
    document.writeValue<std::uint8_t>("save.dat", 0, 1);
    document.undo();
    document.redo();
    CHECK(calls == 3);
    document.removeListener(id);
    document.undo();
    CHECK(calls == 3);
}
