# Memory Optimization: String Attributes in BlockModelSoA

## Problem Description

When loading large block models (e.g., a 1GB file), the application's RAM usage can significantly increase, reaching up to 20GB. Analysis of the `BlockModelSoA` structure, especially within the `Reader::load_from_csv` and `SubprocessReader::load` functions, points to `std::unordered_map<std::string, std::vector<std::string>> string_attributes;` as the primary cause of this excessive memory consumption.

The issues with the current approach are:

1.  **High Overhead per String Object:** Each categorical attribute value for every block is stored as an individual `std::string` object. Even with Small String Optimization (SSO), each `std::string` object has a base memory footprint (pointers, size, capacity) which, when multiplied by millions of blocks, consumes gigabytes of RAM.
2.  **Redundant Data Storage:** For categorical attributes (e.g., "ore_type" with values like "gold", "silver", "copper"), many blocks will share the same string value. In the current implementation, each occurrence of "gold" is stored as a separate `std::string` object, often involving separate heap allocations for its character data. This leads to massive duplication of string data in memory.

For instance, a hypothetical 50 million block model with just 5 string attributes (each with an average string length of 10 characters) could consume over 10GB of RAM *just for these string attributes*, on top of the memory for numerical data.

## Proposed Solution: String Interning (String Pooling)

To drastically reduce memory usage for string attributes, the proposed solution is to implement **string interning (also known as string pooling)**. This technique ensures that each unique string value is stored only once in memory.

The core idea is to replace direct `std::string` storage with integer indices that point to a pool of unique string values.

### Changes to `BlockModelSoA`

1.  **Replace `string_attributes`**: The `std::unordered_map<std::string, std::vector<std::string>> string_attributes;` member will be replaced.
2.  **Introduce `string_attribute_indices`**: A new member, `std::unordered_map<std::string, std::vector<uint32_t>> string_attribute_indices;`, will store integer indices for each block's categorical attribute value. `uint32_t` is chosen to accommodate a large number of unique strings (up to ~4 billion).
3.  **Introduce `string_attribute_pools`**: A new member, `std::unordered_map<std::string, std::vector<std::string>> string_attribute_pools;`, will store the *unique* string values for each categorical attribute.

### Changes to `Reader::load_from_csv` and `SubprocessReader::load`

During the loading process, when a string attribute value is parsed:

1.  A temporary lookup mechanism (e.g., `std::unordered_map<std::string, uint32_t>`) will be used to map string values to their corresponding indices for each attribute.
2.  If a string value is encountered for the first time for a given attribute, it will be added to the `string_attribute_pools` for that attribute, and a new unique index will be assigned.
3.  This index will then be stored in the `string_attribute_indices` vector for the current block and attribute.
4.  If the string value has been seen before, its existing index will be retrieved and stored.

### Benefits

*   **Significant Memory Reduction:** By storing only `uint32_t` (4 bytes) per block per string attribute instead of full `std::string` objects (24-32+ bytes per object, plus heap allocation for content), memory consumption will be drastically reduced. Unique string values will be stored only once.
*   **Improved Cache Performance:** Smaller data structures (vectors of integers) can improve CPU cache locality.
*   **Clarity of Data Representation:** Explicitly separates categorical data values from their representation indices.

This optimization is a fundamental change to how string data is managed and is expected to resolve the excessive RAM usage problem.
