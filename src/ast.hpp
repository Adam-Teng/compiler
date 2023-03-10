#ifndef AST_H
#define AST_H

#include <llvm/IR/Value.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace spc {
struct AbstractNode;
struct DummyNode;
struct ExprNode;
struct LeftValueExprNode;
struct StmtNode;
struct IdentifierNode;
struct TypeNode;
struct StringTypeNode;
struct ConstValueNode;
struct StringNode;
struct FuncExprNode;
struct SysRoutineNode;
struct SysCallNode;
struct ArgListNode;
struct ConstListNode;
struct RoutineCallNode;
struct RoutineNode;
struct ProgramNode;
struct CompoundStmtNode;
struct ProcStmtNode;
struct StmtList;

struct CodegenContext;

template <typename NodeType>
bool is_a_ptr_of(const std::shared_ptr<AbstractNode> &ptr) {
  auto _p = ptr.get();
  return dynamic_cast<NodeType *>(_p) != nullptr;
}

inline std::shared_ptr<AbstractNode> wrap_node(AbstractNode *node) { return std::shared_ptr<AbstractNode>{node}; }

template <typename TNode>
typename std::enable_if<std::is_base_of<AbstractNode, TNode>::value, std::shared_ptr<TNode>>::type cast_node(
    const std::shared_ptr<AbstractNode> &node) {
  if (is_a_ptr_of<TNode>(node)) return std::dynamic_pointer_cast<TNode>(node);
  // std::string nodeTypeName = typeid(*node.get()).name();
  assert(is_a_ptr_of<TNode>(node));
  return nullptr;
}

template <typename NodeType, typename... Args>
std::shared_ptr<AbstractNode> make_node(Args &&...args) {
  return std::dynamic_pointer_cast<AbstractNode>(std::make_shared<NodeType>(std::forward<Args>(args)...));
};

struct AbstractNode : public std::enable_shared_from_this<AbstractNode> {
  std::list<std::shared_ptr<AbstractNode>> _children;
  std::weak_ptr<AbstractNode> _parent;

  virtual ~AbstractNode() noexcept = default;
  virtual llvm::Value *codegen(CodegenContext &context) = 0;
  void print_json() const;
  std::string to_json() const;
  std::list<std::shared_ptr<AbstractNode>> &children() noexcept {
    assert(this->should_have_children());
    return this->_children;
  }
  auto &parent() noexcept { return this->_parent; }
  virtual void add_child(const std::shared_ptr<AbstractNode> &node) {
    this->_children.push_back(node);
    node->parent() = this->shared_from_this();
  }
  virtual void add_child(std::shared_ptr<AbstractNode> &&node) {
    this->_children.push_back(node);
    node->parent() = this->shared_from_this();
  }
  void merge_children(const std::list<std::shared_ptr<AbstractNode>> &children) {
    for (const auto &e : children) {
      this->add_child(e);
    }
  }
  void lift_children(const std::shared_ptr<AbstractNode> &node) { this->merge_children(node->children()); }
  virtual bool should_have_children() const { return true; }
  virtual std::string json_head() const = 0;
};

struct DummyNode : public AbstractNode {
 public:
  std::string json_head() const override { return std::string{"\"type\": \"<unspecified-from-dummy>\""}; }

  llvm::Value *codegen(CodegenContext &context) override {
    std::cout << typeid(*this).name() << std::endl;
    assert(false);
    return nullptr;
  }
};

struct ExprNode : public DummyNode {
  std::shared_ptr<TypeNode> type;

 protected:
  ExprNode() = default;
};

struct LeftValueExprNode : public ExprNode {
  virtual llvm::Value *get_ptr(CodegenContext &context) = 0;
  std::string name;
};

struct StmtNode : public DummyNode {
 protected:
  StmtNode() = default;
};

struct IdentifierNode : public LeftValueExprNode {
 public:
  explicit IdentifierNode(const char *c) {
    name = std::string(c);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });  // ????????????????????????????????????????????????
  }

  llvm::Value *get_ptr(CodegenContext &context) override;
  llvm::Value *codegen(CodegenContext &context) override;

 protected:
  std::string json_head() const override {
    return std::string{"\"type\": \"Identifier\", \"name\": \""} + this->name + "\"";
  }

  bool should_have_children() const override { return false; }
};

struct IdentifierNode;

enum class Type {
  /// ?????????
  UNDEFINED,
  /// ?????????
  STRING,
};

std::string type2string(Type type);

struct TypeNode : public DummyNode {
  Type type = Type::UNDEFINED;
  llvm::Type *get_llvm_type(CodegenContext &context) const;

  TypeNode() = default;
  virtual std::string json_head() const = 0;
  virtual bool should_have_children() const = 0;
};

struct SimpleTypeNode : public TypeNode {
  using TypeNode::type;
  SimpleTypeNode(Type type) { this->type = type; }
  virtual std::string json_head() const override;
  virtual bool should_have_children() const override { return false; }
};

struct StringTypeNode : public TypeNode {
 public:
  StringTypeNode() { type = Type::STRING; }
  virtual std::string json_head() const override;
  virtual bool should_have_children() const override { return false; }
};

struct ConstValueNode : public ExprNode {
 protected:
  ConstValueNode() { type = nullptr; }

  bool should_have_children() const final { return false; }
};

struct StringNode : public ConstValueNode {
 public:
  std::string val;

  StringNode(const char *val) : val(val) {
    this->val.erase(this->val.begin());
    this->val.pop_back();
    type = std::make_shared<SimpleTypeNode>(Type::STRING);
  }

  llvm::Value *codegen(CodegenContext &context) override;

 protected:
  std::string json_head() const override { return std::string{"\"type\": \"String\", \"value\": \""} + val + "\""; }
};

struct FuncExprNode : public ExprNode {
 public:
  std::shared_ptr<AbstractNode> func_call;

  FuncExprNode(const std::shared_ptr<AbstractNode> &func_call) : func_call(func_call) {
    assert(is_a_ptr_of<RoutineCallNode>(func_call) || is_a_ptr_of<SysCallNode>(func_call));
  }

 protected:
  std::string json_head() const override {
    return std::string{"\"type\": \"FuncExpr\", \"call\": "} + this->func_call->to_json();
  }

  bool should_have_children() const override { return false; }
};

enum class SysRoutine {
  /// ???????????????
  WRITELN
};

inline std::string to_string(SysRoutine routine) {
  std::map<SysRoutine, std::string> routine_to_string{{SysRoutine::WRITELN, "writeln"}};
  // TODO: bound checking
  return routine_to_string[routine];
}

struct SysRoutineNode : public DummyNode {
 public:
  SysRoutine routine;

  explicit SysRoutineNode(SysRoutine routine) : routine(routine) {}

 protected:
  bool should_have_children() const override { return false; }
};

struct SysCallNode : public DummyNode {
 public:
  std::shared_ptr<SysRoutineNode> routine;
  std::shared_ptr<ArgListNode> args;

  SysCallNode(const std::shared_ptr<AbstractNode> &routine, const std::shared_ptr<AbstractNode> &args);

  explicit SysCallNode(const std::shared_ptr<AbstractNode> &routine) : SysCallNode(routine, make_node<ArgListNode>()) {}

  llvm::Value *codegen(CodegenContext &context) override;

 protected:
  std::string json_head() const override;

  bool should_have_children() const override { return false; }
};

struct ArgListNode : public DummyNode {
 protected:
  std::string json_head() const override { return std::string{"\"type\": \"ArgList\""}; }

  bool should_have_children() const override { return true; }
};

struct RoutineCallNode : public DummyNode {
 public:
  /// ?????????
  std::shared_ptr<IdentifierNode> identifier;
  /// ??????
  std::shared_ptr<ArgListNode> args;

  RoutineCallNode(const std::shared_ptr<AbstractNode> &identifier, const std::shared_ptr<AbstractNode> &args)
      : identifier(cast_node<IdentifierNode>(identifier)), args(cast_node<ArgListNode>(args)) {}

  explicit RoutineCallNode(const std::shared_ptr<AbstractNode> &identifier)
      : RoutineCallNode(identifier, make_node<ArgListNode>()) {}

 protected:
  std::string json_head() const override {
    return std::string{"\"type\": \"RoutineCall\", \"identifier\": "} + this->identifier->to_json() +
           ", \"args\": " + this->args->to_json();
  }

  bool should_have_children() const final { return false; }
};

/// ??????????????????
struct RoutineNode : public DummyNode {
 public:
  std::shared_ptr<IdentifierNode> name;

  RoutineNode(const std::shared_ptr<AbstractNode> &name) : name(cast_node<IdentifierNode>(name)) {}

 protected:
  RoutineNode() = default;

  bool should_have_children() const final { return true; }
};
/// ??????????????????
struct ProgramNode : public RoutineNode {
 public:
  using RoutineNode::RoutineNode;

  llvm::Value *codegen(CodegenContext &context) override;

 protected:
  std::string json_head() const override {
    return std::string{"\"type\": \"Program\", \"name\": "} + this->name->to_json();
  }
};

struct CompoundStmtNode : public StmtNode {
 protected:
  std::string json_head() const override { return std::string{"\"type\": \"CompoundStmt\""}; }

  bool should_have_children() const override { return true; }
};

struct ProcStmtNode : public StmtNode {
 public:
  std::shared_ptr<AbstractNode> proc_call;

  ProcStmtNode(const std::shared_ptr<AbstractNode> &proc_call) : proc_call(proc_call) {
    assert(is_a_ptr_of<RoutineCallNode>(proc_call) || is_a_ptr_of<SysCallNode>(proc_call));
  }

  llvm::Value *codegen(CodegenContext &context) override;

 protected:
  std::string json_head() const override {
    return std::string{"\"type\": \"ProcStmt\", \"call\": "} + this->proc_call->to_json();
  }

  bool should_have_children() const override { return false; }
};

struct StmtList : public StmtNode {};
}  // namespace spc

#endif
