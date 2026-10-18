#include "workflow.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace example {
Article persist(Article article) {
    static std::vector<Article> store; // In-process demo, not durable storage.
    store.push_back(article);
    return article;
}
Article validate(const Article& article) {
    if (article.title.empty()) throw std::invalid_argument("empty title");
    return article;
}
Article authenticate(const Article& article, const std::string& credential) {
    if (credential != "demo-author") throw std::invalid_argument("invalid demo credential");
    return article;
}
void publish(const Article& article) {
    std::cout << "Published: " << article.title << '\n';
}
}
