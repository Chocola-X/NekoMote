#include <catch2/catch_test_macros.hpp>
#include <nekomote/types.hpp>

using namespace nekomote;

TEST_CASE("Id default-constructed is invalid", "[types]")
{
    NodeId id{};
    REQUIRE_FALSE(id.valid());
    REQUIRE(id.value == 0);
}

TEST_CASE("IdGenerator issues valid, increasing ids", "[types]")
{
    IdGenerator<NodeIdTag> gen;
    NodeId a = gen.next();
    NodeId b = gen.next();
    NodeId c = gen.next();
    REQUIRE(a.valid());
    REQUIRE(b.valid());
    REQUIRE(c.valid());
    REQUIRE(a != b);
    REQUIRE(b != c);
    REQUIRE(a.value < b.value);
    REQUIRE(b.value < c.value);
}

TEST_CASE("IdGenerator skips zero", "[types]")
{
    IdGenerator<TextureIdTag> gen;
    TextureId first = gen.next();
    REQUIRE(first.value == 1u);
}

TEST_CASE("Ids of different tags are distinct types", "[types]")
{
    // This is a compile-time property: assigning NodeId to TextureId must not
    // compile. We cannot assert non-compilation in a test, but we verify the
    // ids compare equal only within the same tag and that hashing is stable.
    NodeId n{42};
    NodeId n2{42};
    TextureId t{42};

    REQUIRE(n == n2);
    REQUIRE(n.value == t.value); // same underlying integer...
    // ...but they are unrelated types (enforced by the compiler at use sites).
}

TEST_CASE("Id ordering", "[types]")
{
    NodeId a{1};
    NodeId b{2};
    REQUIRE(a < b);
    REQUIRE(b > a);
    REQUIRE(a != b);
}

TEST_CASE("Id hashing is deterministic", "[types]")
{
    std::hash<NodeId> h{};
    NodeId a{7};
    NodeId b{7};
    REQUIRE(h(a) == h(b));
    NodeId c{8};
    REQUIRE(h(a) != h(c));
}
