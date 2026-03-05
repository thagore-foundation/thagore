from pathlib import Path
import unittest


class V25QueryIncrementalPipelineParityTests(unittest.TestCase):
    def test_query_header_defines_named_queries_and_cache_api(self) -> None:
        header = Path("compiler/include/thagc/query/query.hpp").read_text(encoding="utf-8")
        self.assertIn("kParseFileQueryName", header)
        self.assertIn("kNameResolveQueryName", header)
        self.assertIn("kTypeCheckFunctionQueryName", header)
        self.assertIn("kBorrowCheckFunctionQueryName", header)
        self.assertIn("kMonomorphizeQueryName", header)
        self.assertIn("kCodegenFunctionQueryName", header)
        self.assertIn("kLinkQueryName", header)
        self.assertIn("class QueryCache", header)
        self.assertIn("std::optional<V> get", header)
        self.assertIn("void put", header)

    def test_driver_builder_routes_parse_file_through_query_cache(self) -> None:
        builder = Path("compiler/src/driver/builder.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "thagc/query/query.hpp"', builder)
        self.assertIn("using ParseFileQueryCache = query::QueryCache<NamedQueryKey, ParseFileQueryResult, NamedQueryKeyHasher>;", builder)
        self.assertIn("run_parse_file_query", builder)
        self.assertIn("query::kParseFileQueryName", builder)


if __name__ == "__main__":
    unittest.main()
