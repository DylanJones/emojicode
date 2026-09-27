//
//  PathAnalyser.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 04/07/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "PathAnalyser.hpp"
#include "Scoping/SemanticScoper.hpp"
#include "CompilerError.hpp"
#include <algorithm>
#include <cassert>

namespace EmojicodeCompiler {

void PathAnalyser::uninitalizedError(const ResolvedVariable &rvar, const SourcePosition &p) const {
    auto incident = PathAnalyserIncident(rvar.inInstanceScope, rvar.variable.id());
    if (!hasCertainly(incident)) {
        if (hasPotentially(incident)) {
            throw CompilerError(p, "Variable \"", utf8(rvar.variable.name()),"\" is not initialized on all paths.");
        }
        throw CompilerError(p, "Variable \"", utf8(rvar.variable.name()),"\" is not initialized.");
    }
}

void PathAnalyser::copyCertainIncidents() {
    auto incs = intersectCertainIncidents(false);
    currentBranch_->certainIncidents.insert(incs.begin(), incs.end());

    // Execution does not continue after a branch that returned (or raised an error, or called a function that never
    // returns), so only the other branches determine whether the instance has been initialized afterwards. The
    // initialization of local variables is not treated this way, as their release at a return does not consider the
    // path (see MFFunctionAnalyser::releaseAllVariables()).
    for (auto &incident : intersectCertainIncidents(true)) {
        if (incident.type() == PathAnalyserIncident::InstanceVarInit ||
            incident.type() == PathAnalyserIncident::CalledSuperInitializer) {
            currentBranch_->certainIncidents.emplace(incident);
        }
    }
}

std::set<PathAnalyserIncident> PathAnalyser::intersectCertainIncidents(bool skipReturned) const {
    std::set<PathAnalyserIncident> incs;
    bool first = true;
    for (auto &branch : currentBranch_->branches) {
        if (skipReturned && branch.certainIncidents.count(PathAnalyserIncident::Returned) > 0) {
            continue;
        }
        if (first) {
            incs = branch.certainIncidents;
            first = false;
            continue;
        }
        auto newIncidents = std::set<PathAnalyserIncident>();
        std::set_intersection(incs.begin(), incs.end(), branch.certainIncidents.begin(),
                              branch.certainIncidents.end(), std::inserter(newIncidents, newIncidents.begin()));
        incs = newIncidents;
    }
    return incs;
}

bool PathAnalyser::hasCertainly(PathAnalyserIncident incident) const {
    for (auto branch = currentBranch_; branch != nullptr; branch = branch->parent) {
        if (branch->certainIncidents.find(incident) != branch->certainIncidents.end()) {
            return true;
        }
    }
    return false;
}

bool PathAnalyser::hasPotentially(PathAnalyserIncident incident) const {
    for (auto branch = currentBranch_; branch != nullptr; branch = branch->parent) {
        if (branch->potentialIncidents.find(incident) != branch->potentialIncidents.end()) {
            return true;
        }
    }
    return false;
}

void PathAnalyser::copyPotentialIncidents() {
    for (auto branch : currentBranch_->branches) {
        currentBranch_->potentialIncidents.insert(branch.potentialIncidents.begin(),
                                                  branch.potentialIncidents.end());
    }
}

void PathAnalyser::finishMutualExclusiveBranches() {
    if (currentBranch_->branches.empty()) {
        return;
    }

    copyPotentialIncidents();
    copyCertainIncidents();

    currentBranch_->branches.clear();
}

void PathAnalyser::endBranch() {
    assert(currentBranch_->branches.empty());
    auto parent = currentBranch_->parent;
    currentBranch_->parent = nullptr;
    currentBranch_ = parent;
}

}  // namespace EmojicodeCompiler
