#pragma once
#include <string>
#include <clangsolv_labels.hpp>

namespace example {
void smartPointerShowcase();
struct Article { std::string title; };
// Returns introduce trusted evidence. The implementation supplies domain work.
returns(Persisted)
Article persist(Article article);
readonly returns(Validated) preserves()
Article validate(const Article& article);
readonly returns(Authenticated) preserves()
Article authenticate(const Article& article, const std::string& credential);
readonly
void publish(needs(Persisted, Validated, Authenticated) const Article& article);
}
