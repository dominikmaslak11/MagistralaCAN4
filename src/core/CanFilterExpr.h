#pragma once
#include "CanFrame.h"
#include <QString>
#include <string>
#include <memory>
#include <vector>
#include <stdexcept>

/// Lightweight Wireshark-style CAN filter expression evaluator.
///
/// Grammar:
///   expr    ::= orExpr
///   orExpr  ::= andExpr ( '||' andExpr )*
///   andExpr ::= unary   ( '&&' unary   )*
///   unary   ::= '!' unary | '(' expr ')' | comparison
///   comparison ::= field op value
///   field   ::= 'can.id' | 'can.dlc' | 'can.data[N]' | 'can.ext'
///              | 'can.fd'  | 'can.err' | 'can.ts'
///   op      ::= '==' | '!=' | '<' | '<=' | '>' | '>='
///   value   ::= decimal | 0xHEX | 'true' | 'false'
///
/// Example: can.id == 0x100 && can.data[0] > 0x50
///          (can.id >= 0x7DF && can.id <= 0x7EF) || can.dlc == 0
///          !can.fd && can.data[1] != 0x00

class CanFilterExpr {
public:
    /// Parse the expression. Throws std::invalid_argument on syntax error.
    static CanFilterExpr parse(const QString &expr);

    /// Evaluate the expression against a frame.
    bool matches(const CanFrame &frame) const;

    /// Returns the last parse error (empty string if parse succeeded).
    QString errorString() const { return m_error; }
    bool    isValid() const     { return !m_error.isEmpty() == false && m_root != nullptr; }

    CanFilterExpr() = default;

private:
    // AST nodes
    struct Node {
        virtual ~Node() = default;
        virtual int64_t eval(const CanFrame &) const = 0;
    };

    struct LiteralNode : Node {
        int64_t value;
        explicit LiteralNode(int64_t v) : value(v) {}
        int64_t eval(const CanFrame &) const override { return value; }
    };

    struct FieldNode : Node {
        enum Field { ID, DLC, DATA, EXT, FD, ERR, TS };
        Field field;
        int   byteIdx = 0;
        FieldNode(Field f, int idx = 0) : field(f), byteIdx(idx) {}
        int64_t eval(const CanFrame &f) const override;
    };

    struct BinaryNode : Node {
        enum Op { EQ, NE, LT, LE, GT, GE, AND, OR };
        Op op;
        std::unique_ptr<Node> left, right;
        BinaryNode(Op o, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
            : op(o), left(std::move(l)), right(std::move(r)) {}
        int64_t eval(const CanFrame &f) const override;
    };

    struct NotNode : Node {
        std::unique_ptr<Node> child;
        explicit NotNode(std::unique_ptr<Node> c) : child(std::move(c)) {}
        int64_t eval(const CanFrame &f) const override { return !child->eval(f); }
    };

    // Parser state
    struct Parser {
        std::vector<QString> tokens;
        size_t pos = 0;

        explicit Parser(const QString &expr);
        std::unique_ptr<Node> parseExpr();
        std::unique_ptr<Node> parseOr();
        std::unique_ptr<Node> parseAnd();
        std::unique_ptr<Node> parseUnary();
        std::unique_ptr<Node> parseComparison();
        std::unique_ptr<Node> parseField(const QString &tok);
        int64_t               parseValue(const QString &tok);
        BinaryNode::Op        parseOp(const QString &tok);
        const QString        &peek() const;
        QString               consume();
        bool                  atEnd() const { return pos >= tokens.size(); }
    };

    std::shared_ptr<Node> m_root;
    QString               m_error;
};
