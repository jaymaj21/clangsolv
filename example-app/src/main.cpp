#include "workflow.hpp"

int main(int argc, char**) {
    using namespace example;
    using namespace example::tags;
    // Construct unlabelled storage before gathering evidence: unknown constructor
    // effects conservatively erase earlier facts in this initial frontend.
    Article chosen;
    const std::string credential="demo-author";
    const auto saved=persist(Article{"Ordinary values, checked workflow states"});
    const auto checked=validate(saved);
    const auto ready=authenticate(checked,credential);
    publish(ready);

    // Native if/else joins evidence. Only Persisted is common to both paths.
    if (argc>1) chosen=checked;
    else chosen=saved;
    clangsolv::require<Persisted>(chosen);
    const auto checkedAgain=validate(chosen);
    const auto readyAgain=authenticate(checkedAgain,credential);
    publish(readyAgain);

    // The fourth label comes entirely from application configuration.
    // This marker explicitly trusts the application's completed approval step.
    clangsolv::establish<Approved>(chosen);
    clangsolv::require<Approved>(chosen);
    clangsolv::forget<Approved>(chosen);
    smartPointerShowcase();
}
