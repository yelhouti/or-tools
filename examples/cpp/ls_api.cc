// Copyright 2011-2012 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "base/hash.h"
#include "base/map-util.h"
#include "base/stl_util.h"
#include "base/random.h"
#include "constraint_solver/constraint_solveri.h"
#include "constraint_solver/constraint_solver.h"
#include "constraint_solver/model.pb.h"

namespace operations_research {
class OneVarLns : public BaseLNS {
 public:
  OneVarLns(const IntVar* const* vars, int size)
      : BaseLNS(vars, size),
        index_(0) {}

  ~OneVarLns() {}

  virtual void InitFragments() { index_ = 0; }

  virtual bool NextFragment(std::vector<int>* fragment) {
    const int size = Size();
    if (index_ < size) {
      fragment->push_back(index_);
      ++index_;
      return true;
    } else {
      return false;
    }
  }

 private:
  int index_;
};

class MoveOneVar: public IntVarLocalSearchOperator {
 public:
  MoveOneVar(const std::vector<IntVar*>& variables)
      : IntVarLocalSearchOperator(variables.data(), variables.size()),
        variable_index_(0),
        move_up_(false) {}

  virtual ~MoveOneVar() {}

 protected:
  // Make a neighbor assigning one variable to its target value.
  virtual bool MakeOneNeighbor() {
    const int64 current_value = OldValue(variable_index_);
    if (move_up_) {
      SetValue(variable_index_, current_value  + 1);
      variable_index_ = (variable_index_ + 1) % Size();
    } else {
      SetValue(variable_index_, current_value  - 1);
    }
    move_up_ = !move_up_;
    return true;
  }

 private:
  virtual void OnStart() {
    CHECK_GE(variable_index_, 0);
    CHECK_LT(variable_index_, Size());
  }

  // Index of the next variable to try to restore
  int64 variable_index_;
  // Direction of the modification.
  bool move_up_;
};

class SumFilter : public IntVarLocalSearchFilter {
 public:
  SumFilter(const IntVar* const* vars, int size) :
      IntVarLocalSearchFilter(vars, size), sum_(0) {}

  ~SumFilter() {}

  virtual void OnSynchronize() {
    sum_ = 0;
    for (int index = 0; index < Size(); ++index) {
      sum_ += Value(index);
    }
  }

  virtual bool Accept(const Assignment* delta,
                      const Assignment* unused_deltadelta) {
    const Assignment::IntContainer& solution_delta = delta->IntVarContainer();
    const int solution_delta_size = solution_delta.Size();

    // The input const Assignment* delta given to Accept() may
    // actually contain "Deactivated" elements, which represent
    // variables that have been freed -- they are not bound to a
    // single value anymore. This happens with LNS-type (Large
    // Neighborhood Search) LocalSearchOperator, which are not used in
    // this example as of 2012-01; and we refer the reader to
    // ./routing.cc for an example of such LNS-type operators.
    //
    // For didactical purposes, we will assume for a moment that a
    // LNS-type operator might be applied. The Filter will still be
    // called, but our filter here won't be able to work, since
    // it needs every variable to be bound (i.e. have a fixed value),
    // in the assignment that it considers. Therefore, we include here
    // a snippet of code that will detect if the input assignment is
    // not fully bound. For further details, read ./routing.cc -- but
    // we strongly advise the reader to first try and understand all
    // of this file.
    for (int i = 0; i < solution_delta_size; ++i) {
      if (!solution_delta.Element(i).Activated()) {
        VLOG(1)
            << "Element #" << i << " of the delta assignment given to"
            << " SumFilter::Accept() is not activated (i.e. its variable"
            << " is not bound to a single value anymore). This means that"
            << " we are in a LNS phase, and the DobbleFilter won't be able"
            << " to filter anything. Returning true.";
        return true;
      }
    }
    int64 new_sum = sum_;
    VLOG(1) << "No LNS, size = " << solution_delta_size;
    for (int index = 0; index < solution_delta_size; ++index) {
      int64 touched_var = -1;
      FindIndex(solution_delta.Element(index).Var(), &touched_var);
      const int64 old_value = Value(touched_var);
      const int64 new_value = solution_delta.Element(index).Value();
      new_sum += new_value - old_value;
    }
    VLOG(1) << "new sum = " << new_sum << ", old sum = " << sum_;
    return new_sum < sum_;
  }

 private:
  int64 sum_;
};

void BasicLns() {
  LOG(INFO) << "Basic LNS";
  Solver s("Sample");
  std::vector<IntVar*> vars;
  s.MakeIntVarArray(4, 0, 4, &vars);
  IntVar* const sum_var = s.MakeSum(vars)->Var();
  OptimizeVar* const obj = s.MakeMinimize(sum_var, 1);
  DecisionBuilder* const db =
      s.MakePhase(vars,
                  Solver::CHOOSE_FIRST_UNBOUND,
                  Solver::ASSIGN_MAX_VALUE);
  OneVarLns one_var_lns(vars.data(), vars.size());
  LocalSearchPhaseParameters* const ls_params =
      s.MakeLocalSearchPhaseParameters(&one_var_lns, db);
  DecisionBuilder* const ls = s.MakeLocalSearchPhase(vars, db, ls_params);
  SolutionCollector* const collector = s.MakeLastSolutionCollector();
  collector->Add(vars);
  collector->AddObjective(sum_var);
  SearchMonitor* const log = s.MakeSearchLog(1000, obj);
  s.Solve(ls, collector, obj, log);
  LOG(INFO) << "Objective value = " << collector->objective_value(0);
}

void BasicLs() {
  LOG(INFO) << "Basic LS";
  Solver s("Sample");
  std::vector<IntVar*> vars;
  s.MakeIntVarArray(4, 0, 4, &vars);
  IntVar* const sum_var = s.MakeSum(vars)->Var();
  OptimizeVar* const obj = s.MakeMinimize(sum_var, 1);
  DecisionBuilder* const db =
      s.MakePhase(vars,
                  Solver::CHOOSE_FIRST_UNBOUND,
                  Solver::ASSIGN_MAX_VALUE);
  MoveOneVar one_var_ls(vars);
  LocalSearchPhaseParameters* const ls_params =
      s.MakeLocalSearchPhaseParameters(&one_var_ls, db);
  DecisionBuilder* const ls = s.MakeLocalSearchPhase(vars, db, ls_params);
  SolutionCollector* const collector = s.MakeLastSolutionCollector();
  collector->Add(vars);
  collector->AddObjective(sum_var);
  SearchMonitor* const log = s.MakeSearchLog(1000, obj);
  s.Solve(ls, collector, obj, log);
  LOG(INFO) << "Objective value = " << collector->objective_value(0);
}

void BasicLsWithFilter() {
  LOG(INFO) << "Basic LS with Filter";
  Solver s("Sample");
  std::vector<IntVar*> vars;
  s.MakeIntVarArray(4, 0, 4, &vars);
  IntVar* const sum_var = s.MakeSum(vars)->Var();
  OptimizeVar* const obj = s.MakeMinimize(sum_var, 1);
  DecisionBuilder* const db =
      s.MakePhase(vars,
                  Solver::CHOOSE_FIRST_UNBOUND,
                  Solver::ASSIGN_MAX_VALUE);
  MoveOneVar one_var_ls(vars);
  std::vector<LocalSearchFilter*> filters;
  filters.push_back(s.RevAlloc(new SumFilter(vars.data(), vars.size())));

  LocalSearchPhaseParameters* const ls_params =
      s.MakeLocalSearchPhaseParameters(&one_var_ls, db, NULL, filters);
  DecisionBuilder* const ls = s.MakeLocalSearchPhase(vars, db, ls_params);
  SolutionCollector* const collector = s.MakeLastSolutionCollector();
  collector->Add(vars);
  collector->AddObjective(sum_var);
  SearchMonitor* const log = s.MakeSearchLog(1000, obj);
  s.Solve(ls, collector, obj, log);
  LOG(INFO) << "Objective value = " << collector->objective_value(0);
}
}  //namespace operations_research

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  operations_research::BasicLns();
  operations_research::BasicLs();
  operations_research::BasicLsWithFilter();
  return 0;
}
