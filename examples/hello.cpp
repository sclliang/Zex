// Zex · 文件与代码阅读器
// 打开文件，专注阅读。Ctrl+F 搜索，Ctrl+G 跳转到行。

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Document {
    std::string name;
    std::string language;
    bool pinned = false;
};

class ReadingList {
public:
    void add(Document document) {
        documents_.push_back(std::move(document));
    }

    void print() const {
        for (const auto& document : documents_) {
            std::cout << document.name << " · "
                      << document.language << '\n';
        }
    }

private:
    std::vector<Document> documents_;
};

int main() {
    ReadingList reading;
    reading.add({"hello.cpp", "C++", true});
    reading.add({"settings.json", "JSON"});
    reading.print();
    return 0;
}
