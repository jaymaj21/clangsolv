#include "workflow.hpp"
#include <memory>
#include <utility>

namespace example {
// These summaries describe Article, rather than shared_ptr/unique_ptr itself.
returns(Persisted) std::shared_ptr<Article> persistShared(Article draft) {
    const auto saved=persist(draft);
    return std::make_shared<Article>(saved);
}
readonly returns(Validated) preserves()
std::shared_ptr<Article> validateShared(const std::shared_ptr<Article>& article) {
    validate(*article);
    return article;
}
readonly void inspectSaved(needs(Persisted, Validated) const Article&) {}

void smartPointerShowcase() {
    using namespace example::tags;
    auto saved=persistShared(Article{"Labels belong to the managed object"});
    auto checked=validateShared(saved);
    auto alias=checked;
    inspectSaved(*alias);
    clangsolv::require<Persisted, Validated>(alias);
    // Mutation/reset/get would erase evidence across all aliases.
    alias.reset();

    auto owner=std::make_unique<Article>(Article{"Exclusive ownership"});
    // Explicit trusted assertion after an application-specific verification.
    clangsolv::establish<Validated>(owner);
    auto moved=std::move(owner);
    clangsolv::require<Validated>(moved);
}
}
