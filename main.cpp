#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double DELTA = 1e-6;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    Document(): id(0), relevance(0.0), rating(0) { }

    Document(int doc_id, double doc_relevance, int doc_raiting): id(doc_id), relevance(doc_relevance), rating(doc_raiting) { }

    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    inline static constexpr int INVALID_DOCUMENT_ID = -1;

    SearchServer() = default;
    
    template <typename StringCollection>
    explicit SearchServer(const StringCollection& stop_words) {
        for (const auto& word : stop_words) {
            if (word.size()) {
                stop_words_.insert(word);
            }
        }
    }
    
    explicit SearchServer(const string& stop_words_text)
        : SearchServer(SplitIntoWords(stop_words_text)) { }    

    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    [[nodiscard]] bool AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        if (document_id < 0 || documents_.count(document_id) > 0 || !IsValidWord(document)) {
            return false;
        }

        const vector<string> words = SplitIntoWordsNoStop(document);

        if (words.empty()) {
            documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
        } else {
            const double inv_word_count = 1 / static_cast<double>(words.size());
            
            for (const string& word : words) {
                word_to_document_freqs_[word][document_id] += inv_word_count;
            }

            documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
        }

        return true;
    }

    template <typename DocumentPredicate>
    optional<vector<Document>> FindTopDocuments(const string& raw_query, DocumentPredicate document_predicate) const {
        const optional<Query> query = ParseQuery(raw_query);
        
        if (!IsValidWord(raw_query) || !query.has_value()) {
            return nullopt;
        }

        vector<Document> result = FindAllDocuments(query.value(), document_predicate);

        sort(result.begin(), result.end(),
             [](const Document& lhs, const Document& rhs) {
                 if (abs(lhs.relevance - rhs.relevance) < DELTA) {
                     return lhs.rating > rhs.rating;
                 } else {
                     return lhs.relevance > rhs.relevance;
                 }
             });
        
        if (result.size() > MAX_RESULT_DOCUMENT_COUNT) {
            result.resize(MAX_RESULT_DOCUMENT_COUNT);
        }

        return result;
    }

    optional<vector<Document>> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(raw_query, [status](int document_id, DocumentStatus doc_status, int rating) { return doc_status == status; });
    }

    optional<vector<Document>> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    optional<tuple<vector<string>, DocumentStatus>> MatchDocument(const string& raw_query, int document_id) const {
        const optional<Query> query = ParseQuery(raw_query);
        if (!IsValidWord(raw_query) || !query.has_value()) {
            return nullopt;
        }

        vector<string> matched_words;

        for (const string& word : query.value().plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }

        for (const string& word : query.value().minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }

        return tuple {matched_words, documents_.at(document_id).status};
    }

    int GetDocumentId(int index) const {
        if (index < 0 || index >= documents_.size()) {
            return SearchServer::INVALID_DOCUMENT_ID;
        }

        size_t doc_index = 0;
        for (const auto& doc : documents_) {
            if (doc_index == index) {
                return doc.first;
            }

            ++doc_index;
        }

        return SearchServer::INVALID_DOCUMENT_ID;
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;

        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }

        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }

        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }

        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    optional<QueryWord> ParseQueryWord(string text) const {
        if (!IsValidWord(text)) {
            return nullopt;
        }

        bool is_minus = false;

        if (text[0] == '-') {
            if (text[1] == '-') {
                return nullopt;
            }

            is_minus = true;
            text = text.substr(1);
        }

        return QueryWord {text, is_minus, IsStopWord(text)};
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    optional<Query> ParseQuery(const string& text) const {
        Query result;

        for (const string& word : SplitIntoWords(text)) {
            const optional<QueryWord> query_word = ParseQueryWord(word);
            if (!query_word.has_value()) {
                return nullopt;
            }

            if (!query_word.value().is_stop) {
                if (query_word.value().is_minus) {
                    result.minus_words.insert(query_word.value().data);
                } else {
                    result.plus_words.insert(query_word.value().data);
                }
            }
        }

        return result;
    }

    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename KeyMapper>
    vector<Document> FindAllDocuments(const Query& query, KeyMapper key_mapper) const {
        map<int, double> document_to_relevance;

        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }

            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto &[document_id, term_freq] : word_to_document_freqs_.at(word)) {
                if (key_mapper(document_id, documents_.at(document_id).status, documents_.at(document_id).rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }

            for (const auto &[document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto &[document_id, relevance] : document_to_relevance) {
            matched_documents.push_back(
                {document_id, relevance, documents_.at(document_id).rating});
        }

        return matched_documents;
    }

    static bool IsValidWord(const string& word) {
        if (word == "-"s) {
            return false;
        }
        
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }
};

void PrintDocument(const Document& document) {
    cout << "{ "s
         << "document_id = "s << document.id << ", "s
         << "relevance = "s << document.relevance << ", "s
         << "rating = "s << document.rating << " }"s << endl;
}

int main() {
    SearchServer search_server("и в на"s);
    // Явно игнорируем результат метода AddDocument, чтобы избежать предупреждения
    // о неиспользуемом результате его вызова
    (void) search_server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {7, 2, 7});
    if (!search_server.AddDocument(1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, {1, 2})) {
        cout << "Документ не был добавлен, так как его id совпадает с уже имеющимся"s << endl;
    }
    if (!search_server.AddDocument(-1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, {1, 2})) {
        cout << "Документ не был добавлен, так как его id отрицательный"s << endl;
    }
    if (!search_server.AddDocument(3, "большой пёс скво\x12рец"s, DocumentStatus::ACTUAL, {1, 3, 2})) {
        cout << "Документ не был добавлен, так как содержит спецсимволы"s << endl;
    }
    if (const auto documents = search_server.FindTopDocuments("--пушистый"s)) {
        for (const Document& document : *documents) {
            PrintDocument(document);
        }
    } else {
        cout << "Ошибка в поисковом запросе"s << endl;
    }
} 
