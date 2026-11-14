#include <doctest.h>

#include <filesystem>

#include "savetinker/i18n.hpp"
#include "test_support.hpp"

TEST_CASE("placeholders are counted") {
    CHECK(st::countPlaceholders("plain") == 0);
    CHECK(st::countPlaceholders("{} of {}") == 2);
    CHECK(st::countPlaceholders("0x{:X}") == 1);
    CHECK(st::countPlaceholders("{{literal}}") == 0);
    CHECK(st::countPlaceholders("broken {") == -1);
    CHECK(st::countPlaceholders("broken }") == -1);
}

TEST_CASE("translator falls back to English") {
    st::Translator translator;
    std::vector<std::string> rejected;
    REQUIRE(translator.load(R"({"Back": "Retour", "{} saves": "{} sauvegardes", "{} of {}": "{}", "Empty": "", "Bad": 3})",
                            nullptr, &rejected));
    CHECK(translator.get("Back") == "Retour");
    CHECK(translator.get("Unknown") == "Unknown");
    CHECK(translator.get("Empty") == "Empty");
    CHECK(translator.format("{} saves", 3) == "3 sauvegardes");
    CHECK(translator.format("{} of {}", 1, 2) == "1 of 2");
    CHECK(rejected == std::vector<std::string>{"Bad", "{} of {}"});
}

TEST_CASE("later files override earlier ones") {
    st::Translator translator;
    REQUIRE(translator.load(R"({"Back": "Retour", "Next": "Suivant"})"));
    REQUIRE(translator.load(R"({"Back": "Précédent"})"));
    CHECK(translator.get("Back") == "Précédent");
    CHECK(translator.get("Next") == "Suivant");
    std::string error;
    CHECK_FALSE(translator.load("[]", &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("shipped translations are valid") {
    const auto folder = std::filesystem::path(SAVETINKER_SOURCE_DIR) / "app" / "lang";
    REQUIRE(std::filesystem::is_directory(folder));
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        st::Translator translator;
        std::string error;
        std::vector<std::string> rejected;
        CHECK_MESSAGE(translator.load(test::readFile(entry.path()), &error, &rejected), entry.path().string());
        CHECK_MESSAGE(rejected.empty(), (entry.path().string() + ": " + (rejected.empty() ? "" : rejected.front())));
        CHECK(translator.size() > 0);
    }
}
