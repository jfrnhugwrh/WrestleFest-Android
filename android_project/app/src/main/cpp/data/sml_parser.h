#ifndef WRESTLEFEST_DATA_SML_PARSER_H
#define WRESTLEFEST_DATA_SML_PARSER_H

// Minimal reader for the game's SML table format ("<SML V1.1>" ...).
//
// The format is a flat tag/value stream:
//
//   <SML V1.1>
//   <RootName>
//   <ARRAY|RecordType> 117
//       {
//       <U16|Id> 0
//       <STR|Name> "TEST"
//       <F32A2|MaxOnGroundTime> 0.500000 10.000000
//       }
//       ...
//   <ARRAYEND|RecordType>
//
// The original engine loads these tables straight into packed structs; we keep
// them as a list of name/value fields per record and bind them into typed rows in
// game_data.cpp. Everything here is dependency-free so it can be unit tested on
// the host (see sml_parser_test.cpp).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wrestlefest {

enum class SmlType {
    U8,
    U16,
    U32,
    S8,
    S16,
    S32,
    F32,
    Str,
    Other,
};

// One "<TYPE|name> value" entry. `count` is the array length for TYPEAn fields
// (e.g. F32A2, S16A24), 1 otherwise.
struct SmlField {
    std::string name;
    SmlType type = SmlType::Other;
    int count = 1;
    std::vector<double> numbers;
    std::string text;
};

struct SmlRecord {
    std::vector<SmlField> fields;

    const SmlField *find(const char *name) const;
    double number(const char *name, double fallback = 0.0) const;
    int integer(const char *name, int fallback = 0) const;
    std::string string(const char *name) const;
    // Vector fields: `index` selects the element, out of range returns fallback.
    double numberAt(const char *name, int index, double fallback = 0.0) const;
};

// One <ARRAY|Type> N ... <ARRAYEND|Type> block.
struct SmlArray {
    std::string name;
    std::vector<SmlRecord> records;
};

// A parsed file. `records` mirrors the first array (most game tables have
// exactly one); `arrays` keeps every top-level block, which the font files need
// (Header + CharTable).
struct SmlDocument {
    std::string root_name;
    std::string array_name;
    std::vector<SmlRecord> records;
    std::vector<SmlArray> arrays;

    const SmlArray *array(const char *name) const;

    bool parse(const char *data, std::size_t size, std::string *error);
};

const char *smlTypeName(SmlType type);

}  // namespace wrestlefest

#endif  // WRESTLEFEST_DATA_SML_PARSER_H
