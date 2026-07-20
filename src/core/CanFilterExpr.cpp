#include "CanFilterExpr.h"
#include <QRegularExpression>
#include <stdexcept>

// ── FieldNode::eval ───────────────────────────────────────────────────────────

int64_t CanFilterExpr::FieldNode::eval(const CanFrame &f) const {
    switch (field) {
        case ID:  return f.id;
        case DLC: return f.dlc;
        case DATA: return (byteIdx >= 0 && byteIdx < f.dlc) ? f.data[byteIdx] : 0;
        case EXT: return f.extended ? 1 : 0;
        case FD:  return f.fd  ? 1 : 0;
        case ERR: return f.error ? 1 : 0;
        case TS:  return static_cast<int64_t>(f.timestamp);
    }
    return 0;
}

// ── BinaryNode::eval ──────────────────────────────────────────────────────────

int64_t CanFilterExpr::BinaryNode::eval(const CanFrame &f) const {
    // Short-circuit for && and ||
    if (op == AND) return left->eval(f) && right->eval(f) ? 1 : 0;
    if (op == OR)  return left->eval(f) || right->eval(f) ? 1 : 0;

    int64_t l = left->eval(f), r = right->eval(f);
    switch (op) {
        case EQ: return l == r ? 1 : 0;
        case NE: return l != r ? 1 : 0;
        case LT: return l <  r ? 1 : 0;
        case LE: return l <= r ? 1 : 0;
        case GT: return l >  r ? 1 : 0;
        case GE: return l >= r ? 1 : 0;
        default: return 0;
    }
}

// ── Tokenizer ─────────────────────────────────────────────────────────────────

CanFilterExpr::Parser::Parser(const QString &expr) {
    static QRegularExpression re(
        R"((0[xX][0-9a-fA-F]+|\d+|can\.\w+(?:\[\d+\])?|==|!=|<=|>=|<|>|&&|\|\||[!()]|true|false|\w+))");
    auto it = re.globalMatch(expr);
    while (it.hasNext())
        tokens.push_back(it.next().captured(0));
}

const QString &CanFilterExpr::Parser::peek() const {
    static const QString empty;
    return atEnd() ? empty : tokens[pos];
}

QString CanFilterExpr::Parser::consume() {
    if (atEnd()) throw std::invalid_argument("Unexpected end of expression");
    return tokens[pos++];
}

// ── Value / operator parsers ──────────────────────────────────────────────────

int64_t CanFilterExpr::Parser::parseValue(const QString &tok) {
    if (tok == "true")  return 1;
    if (tok == "false") return 0;
    bool ok;
    int64_t v = tok.toLongLong(&ok, 0); // base 0: auto-detect 0x prefix
    if (!ok) throw std::invalid_argument("Invalid value: " + tok.toStdString());
    return v;
}

CanFilterExpr::BinaryNode::Op CanFilterExpr::Parser::parseOp(const QString &tok) {
    if (tok == "==") return BinaryNode::EQ;
    if (tok == "!=") return BinaryNode::NE;
    if (tok == "<")  return BinaryNode::LT;
    if (tok == "<=") return BinaryNode::LE;
    if (tok == ">")  return BinaryNode::GT;
    if (tok == ">=") return BinaryNode::GE;
    throw std::invalid_argument("Expected operator, got: " + tok.toStdString());
}

// ── Field parser ──────────────────────────────────────────────────────────────

std::unique_ptr<CanFilterExpr::Node>
CanFilterExpr::Parser::parseField(const QString &tok) {
    if (tok == "can.id")  return std::make_unique<FieldNode>(FieldNode::ID);
    if (tok == "can.dlc") return std::make_unique<FieldNode>(FieldNode::DLC);
    if (tok == "can.ext") return std::make_unique<FieldNode>(FieldNode::EXT);
    if (tok == "can.fd")  return std::make_unique<FieldNode>(FieldNode::FD);
    if (tok == "can.err") return std::make_unique<FieldNode>(FieldNode::ERR);
    if (tok == "can.ts")  return std::make_unique<FieldNode>(FieldNode::TS);

    if (tok.startsWith("can.data[")) {
        int bracket = static_cast<int>(tok.indexOf('['));
        int end     = static_cast<int>(tok.indexOf(']'));
        if (bracket < 0 || end < 0)
            throw std::invalid_argument("Invalid data field: " + tok.toStdString());
        int idx = tok.mid(bracket + 1, end - bracket - 1).toInt();
        return std::make_unique<FieldNode>(FieldNode::DATA, idx);
    }

    throw std::invalid_argument("Unknown field: " + tok.toStdString());
}

// ── Comparison ────────────────────────────────────────────────────────────────

std::unique_ptr<CanFilterExpr::Node> CanFilterExpr::Parser::parseComparison() {
    QString tok = consume();

    // Boolean fields used without comparator: can.fd, can.ext, can.err
    if (tok == "can.fd" || tok == "can.ext" || tok == "can.err") {
        // Check if followed by op
        if (!atEnd() && (peek() == "==" || peek() == "!=")) {
            auto field = parseField(tok);
            auto op    = parseOp(consume());
            auto value = std::make_unique<LiteralNode>(parseValue(consume()));
            return std::make_unique<BinaryNode>(op, std::move(field), std::move(value));
        }
        // No op: treat as boolean expression (== true)
        return parseField(tok);
    }

    if (tok.startsWith("can.")) {
        auto field = parseField(tok);
        auto op    = parseOp(consume());
        auto value = std::make_unique<LiteralNode>(parseValue(consume()));
        return std::make_unique<BinaryNode>(op, std::move(field), std::move(value));
    }

    // Must be a literal used as boolean (unusual, but allow)
    return std::make_unique<LiteralNode>(parseValue(tok));
}

// ── Recursive descent parsers ─────────────────────────────────────────────────

std::unique_ptr<CanFilterExpr::Node> CanFilterExpr::Parser::parseUnary() {
    if (!atEnd() && peek() == "!") {
        consume();
        return std::make_unique<NotNode>(parseUnary());
    }
    if (!atEnd() && peek() == "(") {
        consume(); // (
        auto node = parseExpr();
        if (atEnd() || peek() != ")")
            throw std::invalid_argument("Expected closing ')'");
        consume(); // )
        return node;
    }
    return parseComparison();
}

std::unique_ptr<CanFilterExpr::Node> CanFilterExpr::Parser::parseAnd() {
    auto left = parseUnary();
    while (!atEnd() && peek() == "&&") {
        consume();
        auto right = parseUnary();
        left = std::make_unique<BinaryNode>(BinaryNode::AND, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<CanFilterExpr::Node> CanFilterExpr::Parser::parseOr() {
    auto left = parseAnd();
    while (!atEnd() && peek() == "||") {
        consume();
        auto right = parseAnd();
        left = std::make_unique<BinaryNode>(BinaryNode::OR, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<CanFilterExpr::Node> CanFilterExpr::Parser::parseExpr() {
    return parseOr();
}

// ── Public API ────────────────────────────────────────────────────────────────

CanFilterExpr CanFilterExpr::parse(const QString &expr) {
    CanFilterExpr result;
    if (expr.trimmed().isEmpty()) {
        // Empty expr = match all
        result.m_root = std::make_shared<LiteralNode>(1);
        return result;
    }
    try {
        Parser p(expr);
        auto root = p.parseExpr();
        if (!p.atEnd())
            throw std::invalid_argument("Unexpected token: " + p.peek().toStdString());
        result.m_root = std::shared_ptr<Node>(std::move(root));
    } catch (const std::exception &e) {
        result.m_error = QString::fromStdString(e.what());
        result.m_root  = std::make_shared<LiteralNode>(0); // no match on error
    }
    return result;
}

bool CanFilterExpr::matches(const CanFrame &frame) const {
    if (!m_root) return true;
    return m_root->eval(frame) != 0;
}
