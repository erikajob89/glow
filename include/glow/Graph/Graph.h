#ifndef GLOW_GRAPH_GRAPH_H
#define GLOW_GRAPH_GRAPH_H

#include "glow/Base/Type.h"
#include "glow/Graph/Nodes.h"
#include "glow/Optimizer/Optimizer.h"

#include "llvm/ADT/ArrayRef.h"

#include <list>
#include <unordered_map>
#include <vector>

namespace glow {

using TypesList = std::list<Type>;
using NodesList = std::list<Node *>;
using FunctionList = std::list<Graph *>;
using VariablesList = std::list<Variable *>;
using VariableGradientsList = std::list<std::pair<Variable *, Variable *>>;
using UnsignedArrayRef = llvm::ArrayRef<size_t>;

class Module final {
  /// Stores the functions in the module.
  FunctionList functions_;

public:
  Module() = default;

  ~Module();

  /// \returns True if a function by the name \p name exists in the module.
  bool hasFunction(llvm::StringRef name);
  /// \returns the function with the name \p name, or nullptr if the function does not exist.
  Graph *getFunction(llvm::StringRef name);
  /// \returns a new function with the name \p name.
  Graph *createFunction(llvm::StringRef name);
};

/// Represents the compute graph.
class Graph final : public Named {
public:
  enum class State {
    Created,
    Differentiated,
    Lowered,
    IRGenerated,
  };

private:
  /// A current state of the graph.
  State state_{State::Created};
  /// A uniqued list of types. Types in this list can be equated
  /// by comparing their addresses.
  TypesList types_{};
  /// A list of nodes that the graph owns.
  NodesList nodes_;
  /// A list of variables that the graph owns.
  VariablesList vars_;

  /// A list of (var, grad_var) pairs associating variables with their
  /// gradient variables.
  VariableGradientsList grads_;

  /// Unique index for producing unique names.
  size_t uniqueIdx_;

  /// \returns unique name with a prefix \p Name.
  std::string uniqueName(llvm::StringRef Name);

  /// Unique names defined by node \p N.
  void uniqueNames(Node *N);

public:
  Graph(llvm::StringRef Name = {}) : Named(Name), uniqueIdx_(1) {}

  ~Graph();

  /// Inserts the node \p N to the list of nodes, and returns the inserted node.
  template <class NodeTy> NodeTy *addNode(NodeTy *N) {
    assert(state_ < State::IRGenerated &&
           "Trying to add Node when IR is already generated.");
    uniqueNames(N);
    nodes_.push_back(N);
    return N;
  }

  /// Inserts the variable \p V to the list of variables.
  Variable *addVar(Variable *V) {
    assert(state_ < State::IRGenerated &&
           "Trying to add Variable when IR is already generated.");
    uniqueNames(V);
    vars_.push_back(V);
    return V;
  }

  /// Return a pointer to a uniqued type \p T.
  TypeRef uniqueType(const Type &T);

  /// Return a pointer to a uniqued type \p T.
  TypeRef uniqueType(ElemKind elemTy, llvm::ArrayRef<size_t> dims);

  /// Return a pointer to a uniqued type \p T.
  TypeRef uniqueType(ElemKind elemTy, llvm::ArrayRef<size_t> dims, float scale,
                     int32_t offset);

  /// Return a pointer to a uniqued type \p T.
  /// The new type is identical to \p T, with a new shape \p dims.
  TypeRef uniqueTypeWithNewShape(TypeRef T, llvm::ArrayRef<size_t> dims);

  /// Return the void type.
  TypeRef getVoidTy();

  /// @name High-level, operation-level IRBuilder.
  ///@{

  Variable *createVariable(
      TypeRef T, llvm::StringRef name,
      Variable::VisibilityKind visibility = Variable::VisibilityKind::Private,
      Variable::TrainKind train = Variable::TrainKind::Broadcast,
      float val = 0.0);

  Variable *createVariable(
      ElemKind T, llvm::ArrayRef<size_t> dims, llvm::StringRef name,
      Variable::VisibilityKind visibility = Variable::VisibilityKind::Private,
      Variable::TrainKind train = Variable::TrainKind::Broadcast,
      float val = 0.0);

  Variable *createVariable(
      ElemKind T, llvm::ArrayRef<size_t> dims, float scale, int32_t offset,
      llvm::StringRef name,
      Variable::VisibilityKind visibility = Variable::VisibilityKind::Private,
      Variable::TrainKind train = Variable::TrainKind::Broadcast,
      float val = 0.0);

  ConvolutionNode *createConv(llvm::StringRef name, NodeValue input,
                              size_t depth, size_t kernel, size_t stride,
                              size_t pad);

  ConvolutionNode *createConv(llvm::StringRef name, NodeValue input,
                              NodeValue filter, NodeValue bias, TypeRef outTy,
                              size_t depth, size_t kernel, size_t stride,
                              size_t pad);

  PoolNode *createPool(llvm::StringRef name, NodeValue input,
                       PoolNode::Mode mode, size_t kernel, size_t stride,
                       size_t pad);

  FullyConnectedNode *createFullyConnected(llvm::StringRef name,
                                           NodeValue input, Variable *W,
                                           Variable *B);

  /// Create a fully connected node with the specified output type.
  /// Note, outputDepth is infered based on the output type.
  FullyConnectedNode *createFullyConnected(llvm::StringRef name,
                                           NodeValue input, Node *W, Node *B,
                                           TypeRef outTy);

  /// Create a fully connected node with the given \p name, \p input and \p
  /// output depth. Trainable weight and bias variables are created implicitly.
  FullyConnectedNode *createFullyConnected(llvm::StringRef name,
                                           NodeValue input, size_t outDepth);

  ReluNode *createRELU(llvm::StringRef name, NodeValue input);

  SigmoidNode *createSigmoid(llvm::StringRef name, NodeValue input);

  TanhNode *createTanh(llvm::StringRef name, NodeValue input);

  SoftMaxNode *createSoftMax(llvm::StringRef name, NodeValue input,
                             NodeValue selected);

  CrossEntropyLossNode *createCrossEntropyLoss(llvm::StringRef name,
                                               NodeValue input,
                                               NodeValue labels);

  RegressionNode *createRegression(llvm::StringRef name, NodeValue input,
                                   NodeValue expected);

  ReshapeNode *createReshape(llvm::StringRef name, NodeValue input,
                             UnsignedArrayRef shape);

  TransposeNode *createTranspose(llvm::StringRef name, NodeValue input,
                                 llvm::ArrayRef<unsigned> shuffle);

  BroadcastNode *createBroadcast(llvm::StringRef name, NodeValue input,
                                 UnsignedArrayRef shape, unsigned axis);

  IntrinsicNode *createIntrinsicNode(llvm::StringRef name,
                                     llvm::StringRef identifier,
                                     llvm::ArrayRef<Node *> inputs,
                                     llvm::ArrayRef<TypeRef> outputs);

  ConcatNode *createConcat(llvm::StringRef name, llvm::ArrayRef<Node *> inputs,
                           unsigned dimension);

  SliceNode *createSlice(llvm::StringRef name, NodeValue input,
                         UnsignedArrayRef begin, UnsignedArrayRef end);

  BatchNormalizationNode *createBatchNormalization(llvm::StringRef name,
                                                   NodeValue input,
                                                   size_t channelIdx = 0,
                                                   float epsilon = 1e-5,
                                                   float momentum = 0.9);

  BatchNormalizationNode *
  createBatchNormalization(llvm::StringRef name, NodeValue input,
                           NodeValue beta, NodeValue gamma, NodeValue mean,
                           NodeValue var, size_t channelIdx = 0,
                           float epsilon = 1e-5, float momentum = 0.9);

  LocalResponseNormalizationNode *createLocalResponseNormalization(
      llvm::StringRef name, NodeValue input, size_t halfWindowSize = 2,
      float alpha = 1e-4, float beta = 0.75, float k = 2.0);

  ArithmeticNode *createArithmetic(llvm::StringRef name, NodeValue LHS,
                                   NodeValue RHS, ArithmeticNode::Mode op);

  SelectNode *createSelect(llvm::StringRef name, NodeValue Cond, NodeValue LHS,
                           NodeValue RHS);

  SplatNode *createSplat(llvm::StringRef name, TypeRef ty, float value);

  BatchedMatMulNode *createBatchedMatMul(llvm::StringRef name, NodeValue lhs,
                                         NodeValue rhs);

  BatchedMatMulNode *createBatchedMatMul(llvm::StringRef name, TypeRef outTy,
                                         NodeValue lhs, NodeValue rhs);

  BatchedReduceNode *createBatchedReduce(llvm::StringRef name,
                                         BatchedReduceNode::Mode mode,
                                         NodeValue batch);

  BatchedArithmeticNode *
  createBatchedArithmetic(llvm::StringRef name,
                          BatchedArithmeticNode::Mode mode, NodeValue batch,
                          NodeValue sample);

  BatchedArithmeticNode *
  createBatchedArithmetic(llvm::StringRef name, TypeRef outTy,
                          BatchedArithmeticNode::Mode mode, NodeValue batch,
                          NodeValue sample);

  SaveNode *createSave(llvm::StringRef name, NodeValue input);
  SaveNode *createSave(llvm::StringRef name, NodeValue input, Variable *output);
  /// Create quantization profile node named \p name for the output tensor from
  /// \p input. Capture observed node name in quantization profile node as
  /// original node can be replaced during lowering phase.
  QuantizationProfileNode *createQuantizationProfile(llvm::StringRef name,
                                                     NodeValue input);

  TopKNode *createTopK(llvm::StringRef name, NodeValue input, size_t k);

  GatherNode *createGather(llvm::StringRef name, NodeValue data,
                           NodeValue indices);

  /// Create quantization node which transforms floating point tensor to a
  /// quantized one with given Scale and Offset. Scale and Offset params are
  /// part of the \p outTy.
  QuantizeNode *createQuantize(llvm::StringRef name, NodeValue input,
                               TypeRef outTy);

  /// Create dequantization node which transforms quantized tensor to a
  /// floating point one with given Scale and Offset. Scale and Offset params
  /// are part of the \p input.
  DequantizeNode *createDequantize(llvm::StringRef name, NodeValue input);

  /// Create transformation for quantized tensors to rescale based on the new
  /// Scale and Offset.
  RescaleQuantizedNode *createRescaleQuantized(llvm::StringRef name,
                                               NodeValue input, TypeRef outTy);

  /// Create an unrolled single-layer Simple RNN cell with \p hiddenSize
  /// dimensionality of the hidden state and \p outputSize dimensionality of the
  /// output state. \p inputs define the input for the cell at each time step
  /// and the number of time steps is equal to the size of the \p inputs. The
  /// names of the created variables are prefixed by \p namePrefix.
  /// The output variables are written to \p outputs, they represent the
  /// activations of the output layer, unrolled over time.
  // The dimensionality of the output variables is \p batchSize x \p outputSize.
  void createSimpleRNN(llvm::StringRef namePrefix,
                       const llvm::ArrayRef<Node *> inputs, unsigned batchSize,
                       unsigned hiddenSize, unsigned outputSize,
                       std::vector<Node *> &outputs);

  /// Create an unrolled single-layer GRU cell with \p hiddenSize
  /// dimensionality of the hidden state and \p outputSize dimensionality of the
  /// output state. \p inputs define the input for the cell at each time step
  /// and the number of time steps is equal to the size of the \p inputs. The
  /// names of the created variables are prefixed by \p namePrefix.
  /// The output variables are written to \p outputs, they represent the
  /// activation of the output layer, unrolled over time.
  // The dimensionality of the output variables is \p batchSize x \p outputSize.
  void createGRU(llvm::StringRef namePrefix,
                 const llvm::ArrayRef<Node *> inputs, unsigned batchSize,
                 unsigned hiddenSize, unsigned outputSize,
                 std::vector<Node *> &outputs);

  /// Create an unrolled single-layer LSTM cell with \p hiddenSize
  /// dimensionality of the hidden state and \p outputSize dimensionality of the
  /// output state. \p inputs define the input for the cell at each time step
  /// and the number of time steps is equal to the size of the \p inputs. The
  /// names of the created variables are prefixed by \p namePrefix.
  /// The output variables are written to \p outputs, they represent the
  /// activation of the output layer, unrolled over time.
  // The dimensionality of the output variables is \p batchSize x \p outputSize.
  void createLSTM(llvm::StringRef namePrefix,
                  const llvm::ArrayRef<Node *> inputs, unsigned batchSize,
                  unsigned hiddenSize, unsigned outputSize,
                  std::vector<Node *> &outputs);

  /// @}

  /// Erase a variable from the graph.
  void eraseVariable(Variable *N);
  void eraseVariable(VariablesList::iterator I);

  /// Erase a node from the graph.
  void eraseNode(Node *N);
  void eraseNode(NodesList::iterator I);

  /// Verify the correctness of the graph.
  void verify() const;

  /// Dumps the textual representation of the network.
  void dump() const;

  /// Dump a dotty graph that depicts the function.
  void dumpDAG();

  /// Dump a dotty graph that depicts the function.
  void dumpDAG(const char *dotFilename);

  /// \returns the list of nodes that the graph owns.
  NodesList &getNodes() { return nodes_; }

  const NodesList &getNodes() const { return nodes_; }

  /// \returns a pointer to the first variable with the name \p name or nullptr
  /// if no node has this name.
  Variable *getVariableByName(llvm::StringRef name);

  /// \returns the list of variables that the graph owns.
  VariablesList &getVars() { return vars_; }

  const VariablesList &getVars() const { return vars_; }

  /// Associates a gradient variable \p GradV with the variable \p V.
  void addGradientVariable(Variable *V, Variable *GradV);

  /// \returns a gradient variable associated with \p V.
  /// Returns nullptr if there is no gradient variable
  /// related to this variable.
  Variable *getGradientVariable(Variable *V);

  /// Resets current state to Created.
  void resetState();

  /// Verifies that current state of the graph is not later then \p s
  /// and assigns current state to be \p s.
  void advanceState(State s);
};

struct TrainingConfig;

/// Mutate the inference graph and turn it into a training graph by inserting
/// training (gradient calculation) nodes.
void generateGradientNodes(Graph &G, TrainingConfig &config,
                           CompilationMode mode);

/// \returns a variable that accumulates the gradients that update \p V.
/// Given the variable \p V, find the SGD node that trains it and record the
/// gradients that flow into V.
Variable *generateRecordGradientNode(Graph &G, Variable *V);

} // namespace glow

#endif // GLOW_GRAPH_GRAPH_H
