#include <AIToolbox/Factored/Utils/FasterTrie.hpp>

namespace AIToolbox::Factored {
    FasterTrie::FasterTrie(Factors f) : F(std::move(f)), counter_(0), keys_(F.size()) {
        for (size_t i = 0; i < F.size(); ++i)
            keys_[i].resize(F[i]);
    }

    size_t FasterTrie::insert(PartialFactors pf) {
        keys_[pf.first[0]][pf.second[0]].emplace_back(counter_, std::move(pf));
        return counter_++;
    }

    void FasterTrie::erase(const size_t id, const PartialFactors & pf) {
        // We don't care about ordering here.
        auto & keys = keys_[pf.first[0]][pf.second[0]];
        for (size_t i = 0; i < keys.size(); ++i) {
            if (id == keys[i].first) {
                std::swap(keys[i], keys.back());
                keys.pop_back();
                return;
            }
        }
    }

    std::vector<size_t> FasterTrie::filter(const Factors & f) const {
        std::vector<size_t> retval;

        auto matchPartial = [](const Factors & f, const PartialFactors & pf, const size_t j) {
            // We already know by definition that the first element will match.
            // We also know we can match at most j elements, so we bind that as well.
            for (size_t i = 1; i < j && i < pf.first.size(); ++i) {
                if (pf.first[i] >= f.size())
                    return true;
                if (f[pf.first[i]] != pf.second[i])
                    return false;
            }
            return true;
        };

        size_t i = 0;
        for (; i < f.size(); ++i)
            for (const auto & [id, pf] : keys_[i][f[i]])
                if (matchPartial(f, pf, f.size() - i))
                    retval.push_back(id);

        // We also match to everybody after this point.
        for (; i < keys_.size(); ++i)
            for (const auto & keys : keys_[i])
                for (const auto & id_pf : keys)
                    retval.push_back(id_pf.first);

        return retval;
    }

    std::tuple<std::vector<size_t>, Factors, std::vector<unsigned char>> FasterTrie::reconstruct(const PartialFactors & pf) const {
        std::tuple<std::vector<size_t>, Factors, std::vector<unsigned char>> retval;
        auto & [ids, f, found] = retval;
        f.resize(F.size());
        found.resize(F.size());

        // Copy over set elements, and track them.
        for (size_t i = 0; i < pf.first.size(); ++i) {
            found[pf.first[i]] = true;
            f[pf.first[i]] = pf.second[i];
        }

        // Choice over factor id.
        for (size_t i = 0; i < keys_.size(); ++i) { // Randomize
            // Decide which factor values to iterate over. If the value is
            // known, we only look in the corresponding cell. Otherwise we look
            // at all of them.
            size_t j = 0;
            size_t jLimit = keys_[i].size();
            if (found[i]) {
                j = f[i];
                jLimit = j + 1;
            }
            // Choice over factor value. (randomize)
            do {
                // Choice over entry.
                for (size_t k = 0; k < keys_[i][j].size(); ++k) { // Randomize
                    auto & entry = keys_[i][j][k];
                    auto & entrypf = entry.second;
                    bool match = true;
                    for (size_t q = 0; q < entrypf.first.size(); ++q) {
                        const auto id = entrypf.first[q];
                        if (found[id] && entrypf.second[q] != f[id]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        // We stop the outer loop here since we have now
                        // decided what value this factor has.
                        jLimit = j;
                        ids.push_back(entry.first);
                        for (size_t q = 0; q < entrypf.first.size(); ++q) {
                            const auto id = entrypf.first[q];
                            found[id] = true;
                            f[id] = entrypf.second[q];
                        }
                    }
                }
            } while (++j < jLimit);
        }
        return retval;
    }

    size_t FasterTrie::size() const {
        size_t retval = 0;
        for (const auto & keysF : keys_)
            for (const auto & keysV : keysF)
                retval += keysV.size();

        return retval;
    }

    const Factors & FasterTrie::getF() const { return F; }
}
