
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>
#include <random>
#include "lang.h"
#include "transform.h"
#include "visitor.h"

using namespace std;

// --- Cheat Logic ---

class Renamer : public Transform {
    unordered_map<string, string> var_map;
    unordered_map<string, string> func_map;
    int var_count = 0;
    int func_count = 0;

    string new_var_name() {
        return "v" + to_string(++var_count);
    }
    string new_func_name() {
        return "f" + to_string(++func_count);
    }

public:
    void collect_functions(Program *prog) {
        for (auto decl : prog->body) {
            if (builtinFunctions.find(decl->name) == builtinFunctions.end() && decl->name != "main") {
                if (func_map.find(decl->name) == func_map.end()) {
                    func_map[decl->name] = new_func_name();
                }
            }
        }
    }

    Variable *transformVariable(Variable *node) override {
        if (var_map.count(node->name)) {
            return new Variable(var_map[node->name]);
        }
        return new Variable(node->name);
    }

    Expression *transformCallExpression(CallExpression *node) override {
        string func_name = node->func;
        if (func_map.count(func_name)) {
            func_name = func_map[func_name];
        }
        vector<Expression *> args;
        for (auto arg : node->args) {
            args.push_back(transformExpression(arg));
        }
        return new CallExpression(func_name, args);
    }

    Statement *transformSetStatement(SetStatement *node) override {
        if (var_map.find(node->name->name) == var_map.end()) {
            var_map[node->name->name] = new_var_name();
        }
        return new SetStatement(transformVariable(node->name), transformExpression(node->value));
    }

    Statement *transformBlockStatement(BlockStatement *node) override {
        vector<Statement *> body;
        for (auto stmt : node->body) {
            body.push_back(transformStatement(stmt));
            // Add some dummy code occasionally
            if (rand() % 10 == 0) {
                // (set dummy_v (+ 0 0))
                string dname = "dv" + to_string(rand() % 1000);
                body.push_back(new SetStatement(new Variable(dname), new CallExpression("+", {new IntegerLiteral(0), new IntegerLiteral(0)})));
            }
        }
        return new BlockStatement(body);
    }

    FunctionDeclaration *transformFunctionDeclaration(FunctionDeclaration *node) override {
        string name = node->name;
        if (func_map.count(name)) {
            name = func_map[name];
        }
        
        auto old_var_map = var_map;
        vector<Variable *> params;
        for (auto param : node->params) {
            var_map[param->name] = new_var_name();
            params.push_back(transformVariable(param));
        }
        
        Statement *body = transformStatement(node->body);
        var_map = old_var_map;
        
        return new FunctionDeclaration(name, params, body);
    }

    Program *transformProgram(Program *node) override {
        vector<FunctionDeclaration *> body;
        for (auto decl : node->body) {
            body.push_back(transformFunctionDeclaration(decl));
        }
        // Shuffle functions except main
        if (body.size() > 1) {
            auto main_it = find_if(body.begin(), body.end(), [](FunctionDeclaration* d) { return d->name == "main"; });
            FunctionDeclaration* main_decl = nullptr;
            if (main_it != body.end()) {
                main_decl = *main_it;
                body.erase(main_it);
            }
            static mt19937 g(42);
            shuffle(body.begin(), body.end(), g);
            if (main_decl) body.push_back(main_decl);
        }
        return new Program(body);
    }
};

// --- Anticheat Logic ---

class FeatureExtractor : public Visitor<vector<int>> {
public:
    vector<int> visitProgram(Program *node) override {
        vector<int> features;
        for (auto func : node->body) {
            auto f = visitFunctionDeclaration(func);
            features.insert(features.end(), f.begin(), f.end());
        }
        return features;
    }

    vector<int> visitFunctionDeclaration(FunctionDeclaration *node) override {
        return visitStatement(node->body);
    }

    vector<int> visitStatement(Statement *node) override {
        vector<int> res;
        if (node->is<ExpressionStatement>()) {
            res.push_back(1);
            auto e = visitExpression(node->as<ExpressionStatement>()->expr);
            res.insert(res.end(), e.begin(), e.end());
        } else if (node->is<SetStatement>()) {
            res.push_back(2);
            auto e = visitExpression(node->as<SetStatement>()->value);
            res.insert(res.end(), e.begin(), e.end());
        } else if (node->is<IfStatement>()) {
            res.push_back(3);
            auto e = visitExpression(node->as<IfStatement>()->condition);
            res.insert(res.end(), e.begin(), e.end());
            auto s = visitStatement(node->as<IfStatement>()->body);
            res.insert(res.end(), s.begin(), s.end());
        } else if (node->is<ForStatement>()) {
            res.push_back(4);
            auto s1 = visitStatement(node->as<ForStatement>()->init);
            res.insert(res.end(), s1.begin(), s1.end());
            auto e = visitExpression(node->as<ForStatement>()->test);
            res.insert(res.end(), e.begin(), e.end());
            auto s2 = visitStatement(node->as<ForStatement>()->update);
            res.insert(res.end(), s2.begin(), s2.end());
            auto s3 = visitStatement(node->as<ForStatement>()->body);
            res.insert(res.end(), s3.begin(), s3.end());
        } else if (node->is<BlockStatement>()) {
            res.push_back(5);
            for (auto stmt : node->as<BlockStatement>()->body) {
                auto s = visitStatement(stmt);
                res.insert(res.end(), s.begin(), s.end());
            }
        } else if (node->is<ReturnStatement>()) {
            res.push_back(6);
            auto e = visitExpression(node->as<ReturnStatement>()->value);
            res.insert(res.end(), e.begin(), e.end());
        }
        return res;
    }

    vector<int> visitExpression(Expression *node) override {
        vector<int> res;
        if (node->is<IntegerLiteral>()) {
            res.push_back(7);
        } else if (node->is<Variable>()) {
            res.push_back(8);
        } else if (node->is<CallExpression>()) {
            res.push_back(9);
            // We could add the function name hash here if it's a builtin
            string func = node->as<CallExpression>()->func;
            if (builtinFunctions.count(func)) {
                res.push_back(hash<string>{}(func) % 1000 + 1000);
            }
            for (auto arg : node->as<CallExpression>()->args) {
                auto e = visitExpression(arg);
                res.insert(res.end(), e.begin(), e.end());
            }
        }
        return res;
    }
};

double calculate_similarity(const vector<int>& v1, const vector<int>& v2) {
    if (v1.empty() && v2.empty()) return 1.0;
    if (v1.empty() || v2.empty()) return 0.0;
    
    auto get_ngrams = [](const vector<int>& v, int n) {
        unordered_map<string, int> counts;
        if (v.size() < n) return counts;
        for (size_t i = 0; i <= v.size() - n; ++i) {
            string gram = "";
            for (int j = 0; j < n; ++j) {
                gram += to_string(v[i+j]) + ",";
            }
            counts[gram]++;
        }
        return counts;
    };

    auto compare = [&](int n) {
        auto m1 = get_ngrams(v1, n);
        auto m2 = get_ngrams(v2, n);
        if (m1.empty() || m2.empty()) return 0.0;
        long long common = 0;
        long long total = 0;
        for (auto const& [gram, count] : m1) {
            if (m2.count(gram)) common += min(count, m2[gram]);
            total += count;
        }
        long long total2 = 0;
        for (auto const& [gram, count] : m2) total2 += count;
        return 2.0 * common / (total + total2);
    };

    double s1 = compare(1);
    double s2 = compare(2);
    double s3 = compare(3);
    
    return (s1 + s2 + s3) / 3.0;
}

int main() {
    // Read the first part of the input to decide if it's cheat or anticheat
    // Actually, the problem says:
    // Cheat: scanProgram(std::cin)
    // Anticheat: scanProgram(std::cin) twice, then reference input
    
    // We can try to read one program.
    // If there is more input after the first program, it might be anticheat.
    // But scanProgram might consume "endprogram".
    
    // Let's use a more robust way. Read the whole input into a string first?
    // No, scanProgram uses istream.
    
    // Wait, the problem says:
    // "You can use scanProgram(std::cin) to read in; see sample code."
    // For anticheat: "The standard input stream will sequentially input: 1. A program, ending with endprogram; 2. Another program, ending with endprogram; 3. Reference input for this problem."
    
    // Let's try to read the first program.
    Program *prog1 = scanProgram(cin);
    
    // Check if there's more input.
    removeWhitespaces(cin);
    if (cin.eof()) {
        // Cheat mode
        Renamer renamer;
        renamer.collect_functions(prog1);
        Program *transformed = renamer.transformProgram(prog1);
        cout << transformed->toString() << endl;
    } else {
        // Anticheat mode
        Program *prog2 = scanProgram(cin);
        
        FeatureExtractor extractor;
        vector<int> f1 = extractor.visitProgram(prog1);
        vector<int> f2 = extractor.visitProgram(prog2);
        
        double sim = calculate_similarity(f1, f2);
        cout << sim << endl;
    }
    
    return 0;
}
