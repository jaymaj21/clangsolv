#include "workflow.hpp"
int main(int argc,char**) {
    auto article=example::persist(example::Article{"A title"});
    while (argc>1) {
        clangsolv::require<example::tags::Persisted>(article);
        article.title="Edited inside the loop";
    } // Later iterations cannot assume the original persistence evidence.
}
