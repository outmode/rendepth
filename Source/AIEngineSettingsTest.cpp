#include "AIEngineSettings.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <cstdio>

int main() {
    int failures = 0;
    auto check = [&](const char* json, int expected) {
        rapidjson::Document document;
        document.Parse(json);
        if (document.HasParseError() || AIEngineSettings::read(document) != expected) {
            std::fprintf(stderr, "Unexpected engine preference: %s\n", json);
            ++failures;
        }
    };
    check(R"json({"AI Engine":0})json", 0);
    check(R"json({"AI Engine":1})json", 1);
    check(R"json({"AI Engine":2})json", 2);
    check(R"json({"AI Engine (Restart Required)":1})json", 1);
    check(R"json({"AI Inference (Restart Required)":2})json", 2);
    check(R"json({"AI Engine":1,"AI Engine (Restart Required)":0})json", 1);
    check(R"json({"AI Engine (Restart Required)":2,"AI Inference (Restart Required)":1})json", 2);
    check(R"json({"AI Engine":"CUDA"})json", -1);
    check(R"json({"AI Engine":3})json", -1);
    check(R"json({"AI Engine":-1})json", -1);
    check(R"json({"AI Engine":null,"AI Engine (Restart Required)":1})json", -1);
    check("{}", -1);
    check("[]", -1);
    rapidjson::Document newer;
    newer.Parse(R"json({"AI Engine":1})json");
    // Instance A started on CPU; instance B subsequently selected CUDA.
    if (AIEngineSettings::forSave(0, false, newer) != 1) ++failures;
    // A deliberate CPU selection must still override the saved CUDA choice.
    if (AIEngineSettings::forSave(0, true, newer) != 0) ++failures;
    // After a successful explicit save, later writes preserve newer choices.
    newer[AIEngineSettings::key] = 2;
    if (AIEngineSettings::forSave(1, false, newer) != 2) ++failures;
    rapidjson::Document absent;
    absent.SetObject();
    if (AIEngineSettings::forSave(1, false, absent) != 1) ++failures;
    for (const char* legacy : {"AI Engine (Restart Required)", "AI Inference (Restart Required)"}) {
        rapidjson::Document old;
        old.SetObject();
        old.AddMember(rapidjson::StringRef(legacy), 1, old.GetAllocator());
        rapidjson::Document saved;
        saved.SetObject();
        saved.AddMember(rapidjson::StringRef(AIEngineSettings::key), AIEngineSettings::read(old), saved.GetAllocator());
        rapidjson::StringBuffer buffer;
        rapidjson::Writer writer(buffer);
        saved.Accept(writer);
        check(buffer.GetString(), 1);
        if (saved.HasMember(legacy)) ++failures;
    }
    std::printf("AI engine settings tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
