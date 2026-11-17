#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <codegen/CodegenException.hpp>
#include <codegen/x86/Amd64OnlySubstitutionTable.hpp>
#include <codegen/x86/DecodedInstruction.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/x86/X64OpcodeConstants.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/StubBodyBuilder.hpp>
#include <algorithm>
#include <codegen/x86/VexLegacyLowering.hpp>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace Codegen {

namespace {

template<typename TOperation>
auto _atFileOffset(const Domain::FileByteOffset base, const TOperation& operation) {
    try {
        return operation();
    } catch (const CodegenException& e) {
        throw CodegenException(e.what(), base + e.FailureOffset);
    }
}

constexpr std::int64_t kShortJumpSize = 2;
constexpr std::uint32_t kLoadSegment = 1;
constexpr std::uint32_t kExecutableSegment = 1;
constexpr std::uint8_t kLea = 0x8D;
constexpr std::uint64_t kIslandSearchBytes = 0x100;

struct Placement {
    std::vector<TrampolineSite> Sites;
    std::vector<std::size_t> Claimed;
    std::vector<std::size_t> Consumed;
    std::size_t ReplacementLength = 0;
};

class WindowPlacer {
public:
    WindowPlacer(
        std::vector<std::uint8_t>& seg,
        const std::vector<InstructionMatch>& matches,
        const Domain::ProgramHeader& ph,
        const std::set<std::uint64_t>& branchTargets,
        const std::set<std::size_t>& pending,
        const std::set<std::size_t>& claimed,
        const IAmd64OnlyInstructionMatcher& matcher,
        std::map<std::uint64_t, std::uint64_t>& deadIslands,
        std::function<const IAmd64OnlyInstructionMatcher&(std::uint64_t)> matcherAt
    ) : _seg(seg), _matches(matches), _ph(ph), _branchTargets(branchTargets), _pending(pending), _claimed(claimed), _matcher(matcher), _deadIslands(deadIslands), _matcherAt(std::move(matcherAt)) {}

    [[nodiscard]] std::optional<Placement> Window(const std::size_t index) const {
        std::optional<Placement> fallback;
        for (const bool avoidSegment : {true, false}) {
            if (index + 1 < _matches.size() && (_pending.contains(index + 1) || _movable(index + 1, true, avoidSegment))) {
                if (auto placement = _relocate(index, index, index + 1))
                    return placement;
            }
            if (index > 0 && _movable(index - 1, false, avoidSegment)) {
                if (auto placement = _relocate(index, index - 1, index))
                    return placement;
            }
            if (!fallback && index + 1 < _matches.size() && _pending.contains(index + 1))
                fallback = _relocate(index, index, index + 1, true);
        }
        return fallback;
    }

    [[nodiscard]] std::optional<Placement> Merge(const std::size_t index, const std::size_t first) const {
        for (auto k = first; k < index; ++k)
            if (_blocksFollowing(k) || _address(k + 1) != _end(k))
                return std::nullopt;
        return _relocate(index, first, index, false, true);
    }

    [[nodiscard]] std::optional<Placement> WideWindow(const std::size_t index) const {
        constexpr std::size_t kReach = 5;
        for (std::size_t count = 2; count <= 2 * kReach + 1; ++count) {
            for (std::size_t before = 0; before < count; ++before) {
                const auto after = count - 1 - before;
                if (before > kReach || after > kReach || before > index || index + after >= _matches.size())
                    continue;
                const auto first = index - before;
                const auto last = index + after;
                bool usable = true;
                for (auto k = first; usable && k <= last; ++k) {
                    if (k < last && _address(k + 1) != _end(k))
                        usable = false;
                    else if (k < index)
                        usable = _movable(k, false);
                    else if (k > index)
                        usable = _pending.contains(k) ? !_claimed.contains(k) : _movable(k, k == last);
                }
                if (!usable)
                    continue;
                if (auto placement = _relocate(index, first, last, false, true))
                    return placement;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Placement> Island(const std::size_t index) {
        const auto store = _address(index);
        for (std::size_t distance = 1;; ++distance) {
            bool inReach = false;
            for (const bool before : {true, false}) {
                if (before ? distance > index : index + distance >= _matches.size())
                    continue;
                const auto start = before ? index - distance : index + distance;
                if (_address(start) + kIslandSearchBytes < store || _address(start) > store + kIslandSearchBytes)
                    continue;
                inReach = true;
                if (auto placement = _islandAt(index, start))
                    return placement;
            }
            if (!inReach)
                return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<Placement> IslandOverPending(const std::size_t index) {
        _takePending = true;
        auto placement = Island(index);
        _takePending = false;
        return placement;
    }

    [[nodiscard]] std::optional<Placement> DeadIsland(const std::size_t index) {
        using Amd64OnlySubstitutionTable::kJmpRel32;
        if (_matches[index].Length < kShortJumpSize)
            return std::nullopt;
        const auto from = _address(index) + kShortJumpSize;
        const auto offset = _matches[index].Offset;
        const auto low = offset > kIslandSearchBytes ? offset - kIslandSearchBytes : 0;
        auto k = static_cast<std::size_t>(std::lower_bound(_matches.begin(), _matches.end(), low, [](const auto& candidate, const std::uint64_t value) { return candidate.Offset < value; }) - _matches.begin());
        std::optional<std::uint64_t> best;
        for (; k + 1 < _matches.size() && _matches[k].Offset <= offset + kIslandSearchBytes; ++k) {
            if (!_endsFlow(k))
                continue;
            const auto runStart = _end(k);
            const auto entry = _branchTargets.lower_bound(runStart);
            for (auto j = k + 1; j < _matches.size() && _address(j) == _end(j - 1) && _padding(j) && !_pending.contains(j) && (!_claimed.contains(j) || _deadOverlap(_address(j), _end(j))); ++j) {
                if (entry != _branchTargets.end() && *entry < _end(j))
                    break;
                if (_end(j) - runStart < kJmpRel32.Size)
                    continue;
                for (auto island = std::clamp(from, runStart, _end(j) - kJmpRel32.Size); island + kJmpRel32.Size <= _end(j); ++island) {
                    const auto reach = _distance(island, from);
                    if (reach > std::numeric_limits<std::int8_t>::max())
                        break;
                    if (reach < std::numeric_limits<std::int8_t>::min() || _deadOverlap(island, island + kJmpRel32.Size))
                        continue;
                    if (!best || _gap(island, from) < _gap(*best, from))
                        best = island;
                    break;
                }
            }
        }
        if (!best)
            return std::nullopt;
        try {
            auto placement = _buildDeadIsland(index, *best);
            _deadIslands[*best] = *best + kJmpRel32.Size;
            return placement;
        } catch (const CodegenException&) {
            return std::nullopt;
        }
    }

private:
    std::vector<std::uint8_t>& _seg;
    const std::vector<InstructionMatch>& _matches;
    const Domain::ProgramHeader& _ph;
    const std::set<std::uint64_t>& _branchTargets;
    const std::set<std::size_t>& _pending;
    const std::set<std::size_t>& _claimed;
    const IAmd64OnlyInstructionMatcher& _matcher;
    std::map<std::uint64_t, std::uint64_t>& _deadIslands;
    std::function<const IAmd64OnlyInstructionMatcher&(std::uint64_t)> _matcherAt;
    bool _takePending = false;
    X64InstructionDecoder _decoder;

    [[nodiscard]] bool _laterPending(const std::size_t index, const std::size_t k) const {
        return _takePending && k > index && _pending.contains(k) && !_claimed.contains(k);
    }

    [[nodiscard]] static std::uint64_t _gap(const std::uint64_t a, const std::uint64_t b) { return a > b ? a - b : b - a; }

    [[nodiscard]] bool _endsFlow(const std::size_t index) const {
        const auto kind = _info(index).FlowKind;
        return kind == ControlFlowKind::UnconditionalJump || kind == ControlFlowKind::Return || kind == ControlFlowKind::IndirectJump || kind == ControlFlowKind::Trap;
    }

    [[nodiscard]] bool _deadOverlap(const std::uint64_t start, const std::uint64_t end) const {
        const auto next = _deadIslands.lower_bound(start);
        if (next != _deadIslands.end() && next->first < end)
            return true;
        return next != _deadIslands.begin() && std::prev(next)->second > start;
    }

    [[nodiscard]] std::uint64_t _address(const std::size_t index) const { return _ph.MappedAddress + _matches[index].Offset; }
    [[nodiscard]] std::uint64_t _end(const std::size_t index) const { return _address(index) + _matches[index].Length; }
    [[nodiscard]] static std::int64_t _distance(const std::uint64_t to, const std::uint64_t from) { return static_cast<std::int64_t>(to - from); }
    [[nodiscard]] std::span<const std::uint8_t> _bytes(const std::size_t index) const { return {_seg.data() + _matches[index].Offset, _matches[index].Length}; }
    [[nodiscard]] DecodedInstructionInfo _info(const std::size_t index) const { return _decoder.DecodeInstruction(_seg.data() + _matches[index].Offset, _matches[index].Length); }

    [[nodiscard]] bool _entered(const std::uint64_t start, const std::uint64_t end) const {
        const auto hit = _branchTargets.upper_bound(start);
        return hit != _branchTargets.end() && *hit < end;
    }

    [[nodiscard]] bool _int3(const std::size_t index) const {
        const auto bytes = _bytes(index);
        return bytes.size() == 1 && bytes[0] == X64OpcodeConstants::OneByteInt3;
    }

    [[nodiscard]] bool _padding(const std::size_t index) const {
        const auto info = _info(index);
        if (info.IsTwoByteOpcode)
            return info.Opcode == X64OpcodeConstants::TwoByteNopModRm;
        if (info.Opcode != X64OpcodeConstants::OneByteNop)
            return _int3(index);
        const auto prefixes = _bytes(index).first(info.OpcodeOffset);
        return (info.RexPrefix & X64OpcodeConstants::RexBBit) == 0 && std::find(prefixes.begin(), prefixes.end(), X64OpcodeConstants::PrefixRep) == prefixes.end();
    }

    [[nodiscard]] bool _mayBeEntered(const std::size_t index) const {
        for (auto previous = index; previous > 0; --previous) {
            if (_end(previous - 1) != _address(previous) || _blocksFollowing(previous - 1))
                return true;
            if (!_padding(previous - 1))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool _enteredWithin(const std::size_t first, const std::size_t last) const {
        if (_entered(_address(first), _end(last)))
            return true;
        for (auto index = first + 1; index <= last; ++index) {
            if (!_int3(index) && _mayBeEntered(index))
                return true;
        }
        return false;
    }

    [[nodiscard]] bool _blocksFollowing(const std::size_t index) const {
        const auto kind = _info(index).FlowKind;
        return kind != ControlFlowKind::Sequential && kind != ControlFlowKind::ConditionalBranch;
    }

    [[nodiscard]] bool _movable(const std::size_t index, const bool last, const bool avoidSegment = false) const {
        return !_claimed.contains(index) && !_pending.contains(index) && (last || !_blocksFollowing(index)) && !(avoidSegment && _info(index).SegmentPrefix != 0);
    }

    [[nodiscard]] TrampolineSite _site(const std::uint64_t start, const std::size_t length, LoweredBody body) const {
        const auto offset = static_cast<std::size_t>(start - _ph.MappedAddress);
        return {
            static_cast<Domain::FileByteOffset>(_ph.Offset + offset),
            static_cast<Domain::VirtualAddress>(start),
            length,
            std::vector<std::uint8_t>(_seg.begin() + static_cast<std::ptrdiff_t>(offset), _seg.begin() + static_cast<std::ptrdiff_t>(offset + length)),
            std::move(body.Bytes),
            body.ReturnBranchOffset,
            std::move(body.Relocations)
        };
    }

    [[nodiscard]] std::optional<Placement> _relocate(const std::size_t index, const std::size_t first, const std::size_t last, const bool allowInPlace = false, const bool allowIndirectCall = false) const {
        if (_enteredWithin(first, last) || _end(last) - _address(first) < Amd64OnlySubstitutionTable::kJmpRel32.Size)
            return std::nullopt;
        StubBodyBuilder body;
        Placement placement;
        try {
            for (auto k = first; k <= last; ++k) {
                if (_pending.contains(k)) {
                    _matcher.EmitOutOfLine(body, _bytes(k), allowInPlace);
                    if (k != index)
                        placement.Consumed.push_back(k);
                } else {
                    body.MoveControlFlow(_bytes(k), _info(k), allowIndirectCall);
                }
                placement.Claimed.push_back(k);
            }
        } catch (const CodegenException&) {
            return std::nullopt;
        }
        placement.Sites.push_back(_site(_address(first), static_cast<std::size_t>(_end(last) - _address(first)), body.Finish()));
        placement.ReplacementLength = placement.Sites.back().Body.size();
        return placement;
    }

    [[nodiscard]] std::optional<Placement> _islandAt(const std::size_t index, const std::size_t start) {
        using Amd64OnlySubstitutionTable::kJmpRel32;
        std::size_t head = 0;
        for (auto last = start; last < _matches.size(); ++last) {
            if ((!_laterPending(index, last) && !_movable(last, true)) || _enteredWithin(start, last))
                return std::nullopt;
            if (head == 0 && _end(last) - _address(start) >= kJmpRel32.Size)
                head = static_cast<std::size_t>(_end(last) - _address(start));
            const auto island = _address(start) + head;
            const auto reach = _distance(island, _address(index) + kShortJumpSize);
            if (head != 0 && (reach < std::numeric_limits<std::int8_t>::min() || reach > std::numeric_limits<std::int8_t>::max()))
                return std::nullopt;
            if (head != 0 && _end(last) >= island + kJmpRel32.Size) {
                try {
                    return _buildIsland(index, start, last, head);
                } catch (const CodegenException&) {
                    return std::nullopt;
                }
            }
            if (_blocksFollowing(last))
                return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] Placement _buildDeadIsland(const std::size_t index, const std::uint64_t island) {
        using Amd64OnlySubstitutionTable::kJmpRel32;
        const auto store = _address(index);
        Placement placement;
        StubBodyBuilder lowered;
        lowered.Seek(_distance(store, island));
        _matcher.EmitOutOfLine(lowered, _bytes(index));
        lowered.JumpTo(_distance(_end(index), island));
        placement.Sites.push_back(_site(island, kJmpRel32.Size, lowered.Finish()));
        placement.ReplacementLength = placement.Sites.back().Body.size();
        placement.Claimed.push_back(index);
        const auto start = island - _ph.MappedAddress;
        for (auto k = static_cast<std::size_t>(std::upper_bound(_matches.begin(), _matches.end(), start, [](const std::uint64_t value, const auto& candidate) { return value < candidate.Offset; }) - _matches.begin()); k > 0 && _end(k - 1) > island; --k)
            placement.Claimed.push_back(k - 1);
        for (auto k = static_cast<std::size_t>(std::upper_bound(_matches.begin(), _matches.end(), start, [](const std::uint64_t value, const auto& candidate) { return value < candidate.Offset; }) - _matches.begin()); k < _matches.size() && _address(k) < island + kJmpRel32.Size; ++k)
            placement.Claimed.push_back(k);
        const auto offset = _matches[index].Offset;
        std::fill_n(_seg.begin() + static_cast<std::ptrdiff_t>(offset), _matches[index].Length, Amd64OnlySubstitutionTable::kTrapFill);
        _seg[offset] = X64OpcodeConstants::OneByteJmpRel8;
        _seg[offset + 1] = static_cast<std::uint8_t>(static_cast<std::int8_t>(_distance(island, store + kShortJumpSize)));
        return placement;
    }

    [[nodiscard]] Placement _buildIsland(const std::size_t index, const std::size_t start, const std::size_t last, const std::size_t head) {
        using Amd64OnlySubstitutionTable::kJmpRel32;
        const auto windowStart = _address(start);
        const auto windowEnd = _end(last);
        const auto island = windowStart + head;
        const auto store = _address(index);
        Placement placement;

        StubBodyBuilder moved;
        for (auto k = start; k <= last; ++k) {
            if (_laterPending(index, k)) {
                _matcherAt(_matches[k].Offset).EmitOutOfLine(moved, _bytes(k), true);
                placement.Consumed.push_back(k);
            } else {
                moved.MoveControlFlow(_bytes(k), _info(k));
            }
            placement.Claimed.push_back(k);
        }
        if (!_blocksFollowing(last))
            moved.JumpTo(static_cast<std::int64_t>(windowEnd - windowStart));
        placement.Sites.push_back(_site(windowStart, head, moved.Finish()));

        StubBodyBuilder lowered;
        lowered.Seek(_distance(store, island));
        _matcher.EmitOutOfLine(lowered, _bytes(index));
        lowered.JumpTo(_distance(_end(index), island));
        placement.Sites.push_back(_site(island, kJmpRel32.Size, lowered.Finish()));
        placement.ReplacementLength = placement.Sites.back().Body.size();
        placement.Claimed.push_back(index);

        const auto offset = _matches[index].Offset;
        std::fill_n(_seg.begin() + static_cast<std::ptrdiff_t>(offset), _matches[index].Length, Amd64OnlySubstitutionTable::kTrapFill);
        _seg[offset] = X64OpcodeConstants::OneByteJmpRel8;
        _seg[offset + 1] = static_cast<std::uint8_t>(static_cast<std::int8_t>(_distance(island, store + kShortJumpSize)));
        return placement;
    }
};

class Amd64OnlyConverter : public IAmd64OnlyConverter {
public:
    Amd64OnlyConverter(const Amd64OnlyTarget target, std::function<void(std::uint64_t, std::uint64_t)> progress, std::optional<UpperShadowContext> shadow)
        : _matcher(MakeAmd64OnlyInstructionMatcher(target)),
          _shadowMatcher(shadow && target == Amd64OnlyTarget::Sse ? MakeAmd64OnlyInstructionMatcher(target, shadow->Offset) : nullptr),
          _reportUnlowered(target == Amd64OnlyTarget::Sse),
          _functions(shadow ? shadow->Functions : std::vector<std::pair<std::uint64_t, std::uint64_t>>{}),
          progress(std::move(progress)) {
        std::sort(_functions.begin(), _functions.end());
    }

    [[nodiscard]] ConvertResult Convert(
        std::vector<std::uint8_t> fileBytes,
        const std::vector<Domain::ProgramHeader>& segments
    ) const override;

private:
    struct Pending {
        std::size_t Index;
        InstructionMatch Instruction;
        Amd64OnlyMatch Substitution;
    };

    std::unique_ptr<IAmd64OnlyInstructionMatcher> _matcher;
    std::unique_ptr<IAmd64OnlyInstructionMatcher> _shadowMatcher;
    bool _reportUnlowered;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> _functions;
    std::function<void(std::uint64_t, std::uint64_t)> progress;

    [[nodiscard]] std::optional<std::size_t> _functionAt(const std::uint64_t address) const {
        auto it = std::upper_bound(_functions.begin(), _functions.end(), std::pair{address, std::numeric_limits<std::uint64_t>::max()});
        if (it == _functions.begin()) return std::nullopt;
        --it;
        if (address >= it->second) return std::nullopt;
        return static_cast<std::size_t>(it - _functions.begin());
    }

    void _convertSegment(
        std::vector<std::uint8_t>& fileBytes,
        const Domain::ProgramHeader& ph,
        const std::vector<Domain::ProgramHeader>& segments,
        ConvertResult& result,
        std::uint64_t completed,
        std::uint64_t total
    ) const;

    [[nodiscard]] static std::set<std::uint64_t> _collectBranchTargets(
        const std::vector<std::uint8_t>& seg,
        const std::vector<InstructionMatch>& matches,
        const Domain::ProgramHeader& ph,
        const std::vector<std::uint8_t>& fileBytes,
        const std::vector<Domain::ProgramHeader>& segments
    );
};

std::set<std::uint64_t> Amd64OnlyConverter::_collectBranchTargets(
    const std::vector<std::uint8_t>& seg,
    const std::vector<InstructionMatch>& matches,
    const Domain::ProgramHeader& ph,
    const std::vector<std::uint8_t>& fileBytes,
    const std::vector<Domain::ProgramHeader>& segments
) {
    const auto startsInstruction = [&](const std::uint64_t address) {
        const auto offset = address - ph.MappedAddress;
        const auto match = std::lower_bound(matches.begin(), matches.end(), offset, [](const InstructionMatch& item, const std::uint64_t value) { return item.Offset < value; });
        return match != matches.end() && match->Offset == offset;
    };
    const auto pastPrefixes = [&](const std::uint64_t address) {
        const auto offset = address - ph.MappedAddress;
        const auto after = std::upper_bound(matches.begin(), matches.end(), offset, [](const std::uint64_t value, const InstructionMatch& item) { return value < item.Offset; });
        if (after == matches.begin())
            return false;
        const auto& match = *std::prev(after);
        const auto inside = offset - match.Offset;
        return inside != 0 && inside < match.Length && inside > DecodedInstruction{seg.data() + match.Offset, match.Length}.OpcodeOffset();
    };
    const X64InstructionDecoder decoder;
    std::set<std::uint64_t> targets;
    for (const auto& match : matches) {
        const auto info = decoder.DecodeInstruction(seg.data() + match.Offset, match.Length);
        const auto next = ph.MappedAddress + match.Offset + info.Length;
        if (info.HasBranchTarget && !info.HasRipRelativeDisp) {
            const auto displacement = static_cast<std::uint64_t>(info.BranchDisp);
            const auto target = next + displacement;
            const auto wrapped = info.BranchDisp < 0 ? target > next : target < next;
            if (!wrapped && !pastPrefixes(target))
                targets.insert(target);
        } else if (info.HasRipRelativeDisp && info.Opcode == kLea) {
            std::int32_t displacement;
            std::memcpy(&displacement, seg.data() + match.Offset + info.RipRelativeDispOffset, sizeof(displacement));
            auto table = next + static_cast<std::uint64_t>(displacement);
            if (info.AddressSize32) table = static_cast<std::uint32_t>(table);
            for (const auto& data : segments) {
                if (data.Type != kLoadSegment || table < data.MappedAddress || table - data.MappedAddress >= data.FileSize)
                    continue;
                if (data.Offset > fileBytes.size() || data.FileSize > fileBytes.size() - data.Offset)
                    throw CodegenException("Segment exceeds the file", data.Offset);
                for (auto entry = table; entry - data.MappedAddress + sizeof(std::int32_t) <= data.FileSize; entry += sizeof(std::int32_t)) {
                    std::int32_t relative;
                    std::memcpy(&relative, fileBytes.data() + data.Offset + (entry - data.MappedAddress), sizeof(relative));
                    const auto target = table + static_cast<std::uint64_t>(static_cast<std::int64_t>(relative));
                    if (!startsInstruction(target))
                        break;
                    targets.insert(target);
                }
            }
        }
    }
    return targets;
}

void Amd64OnlyConverter::_convertSegment(
    std::vector<std::uint8_t>& fileBytes,
    const Domain::ProgramHeader& ph,
    const std::vector<Domain::ProgramHeader>& segments,
    ConvertResult& result,
    std::uint64_t completed,
    std::uint64_t total
) const {
    if (ph.Offset > fileBytes.size() || ph.FileSize > fileBytes.size() - ph.Offset)
        throw CodegenException("Code segment exceeds the file", ph.Offset);
    const auto segOffset = static_cast<std::size_t>(ph.Offset);
    const auto segSize = static_cast<std::size_t>(ph.FileSize);

    std::vector<std::uint8_t> seg(
        fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset),
        fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset + segSize)
    );

    const auto scanner = MakeInstructionScanner([&](auto offset, auto) { if (progress) progress(completed + offset, total); });
    const auto matches = _atFileOffset(ph.Offset, [&] { return scanner->ScanCodeSection(seg, 0, seg.size()); });
    std::set<std::size_t> wideFunctions;
    if (_shadowMatcher) {
        for (const auto& match : matches) {
            if (!VexWide(seg.data() + match.Offset, match.Length)) continue;
            if (const auto function = _functionAt(ph.MappedAddress + match.Offset)) wideFunctions.insert(*function);
        }
    }
    const auto matcherAt = [&](const std::uint64_t offset) -> const IAmd64OnlyInstructionMatcher& {
        if (_shadowMatcher) {
            const auto function = _functionAt(ph.MappedAddress + offset);
            if (!function || wideFunctions.contains(*function)) return *_shadowMatcher;
        }
        return *_matcher;
    };

    if (progress) progress(completed + segSize, total);
    std::vector<Pending> pending;
    for (std::size_t index = 0; index < matches.size(); ++index) {
        if (progress && index != 0 && index % 4096 == 0) progress(completed + segSize + matches[index].Offset, total);
        const auto& match = matches[index];
        auto substitution = _atFileOffset(ph.Offset + match.Offset, [&] { return matcherAt(match.Offset).Match(seg.data() + match.Offset, match.Length); });
        if (substitution.has_value())
            pending.push_back({index, match, std::move(*substitution)});
        else if (_reportUnlowered)
            if (const auto form = VexFormName(seg.data() + match.Offset, match.Length)) ++(VexMaskOperation(seg.data() + match.Offset, match.Length) ? result.Avx512Mask : result.Unlowered)[*form];
    }
    if (progress) progress(completed + std::uint64_t{2} * segSize, total);
    if (pending.empty()) {
        if (progress) progress(completed + std::uint64_t{3} * segSize, total);
        return;
    }

    const bool needsBranchTargets = std::any_of(pending.begin(), pending.end(), [](const Pending& item) {
        return item.Substitution.Lowering == Amd64OnlyLowering::Trampoline;
    });
    const auto branchTargets = needsBranchTargets ? _collectBranchTargets(seg, matches, ph, fileBytes, segments) : std::set<std::uint64_t>{};

    std::set<std::size_t> pendingIndices;
    for (const auto& item : pending)
        pendingIndices.insert(item.Index);
    std::set<std::size_t> claimed;
    std::set<std::size_t> consumed;
    std::map<std::uint64_t, std::uint64_t> deadIslands;

    const auto countKept = [&](const auto& kept) {
        if (!_reportUnlowered) return;
        if (const auto form = VexFormName(seg.data() + kept.Offset, kept.Length)) ++result.Unlowered[*form];
    };
    std::size_t processed = 0;
    for (const auto& item : pending) {
        if (progress && processed != 0 && processed % 256 == 0) progress(completed + std::uint64_t{2} * segSize + item.Instruction.Offset, total);
        ++processed;
        if (consumed.contains(item.Index))
            continue;
        const auto& match = item.Instruction;
        const auto& substitution = item.Substitution;
        const auto fileOffset = static_cast<Domain::FileByteOffset>(ph.Offset + match.Offset);
        const auto address = static_cast<Domain::VirtualAddress>(ph.MappedAddress + match.Offset);
        std::size_t replacementLength = 0;

        switch (substitution.Lowering) {
        case Amd64OnlyLowering::InPlace: {
            if (substitution.ReplacementBytes.size() != match.Length)
                throw CodegenException("Intel substitution changes the instruction length", fileOffset);
            std::copy(substitution.ReplacementBytes.begin(), substitution.ReplacementBytes.end(), seg.begin() + static_cast<std::ptrdiff_t>(match.Offset));
            replacementLength = substitution.ReplacementBytes.size();
            ++result.ReplacedCount;
            if (_reportUnlowered)
                pendingIndices.erase(item.Index);
            break;
        }
        case Amd64OnlyLowering::Trampoline: {
            if (substitution.RelocatableWindow && match.Length < Amd64OnlySubstitutionTable::kJmpRel32.Size) {
                WindowPlacer placer(seg, matches, ph, branchTargets, pendingIndices, claimed, matcherAt(match.Offset), deadIslands, matcherAt);
                auto placement = placer.Window(item.Index);
                if (!placement)
                    placement = placer.Island(item.Index);
                if (!placement && _reportUnlowered)
                    placement = placer.WideWindow(item.Index);
                if (!placement && _reportUnlowered && !result.Trampolines.empty()) {
                    const auto& previous = result.Trampolines.back();
                    if (static_cast<std::uint64_t>(previous.Address) + previous.Length == static_cast<std::uint64_t>(address) && static_cast<std::uint64_t>(previous.Address) >= ph.MappedAddress) {
                        const auto start = static_cast<std::uint64_t>(previous.Address) - ph.MappedAddress;
                        const auto first = std::lower_bound(matches.begin(), matches.end(), start, [](const auto& candidate, const std::uint64_t offset) { return candidate.Offset < offset; });
                        if (first != matches.end() && first->Offset == start) {
                            if (auto merged = placer.Merge(item.Index, static_cast<std::size_t>(first - matches.begin()))) {
                                result.Trampolines.pop_back();
                                placement = std::move(merged);
                            }
                        }
                    }
                }
                if (!placement)
                    placement = placer.DeadIsland(item.Index);
                if (!placement)
                    placement = placer.IslandOverPending(item.Index);
                if (!placement && substitution.Optional) {
                    ++result.KeptCount;
                    result.Reports.push_back({substitution.InstructionName, fileOffset, match.Length, 0, Amd64OnlyLowering::Kept});
                    countKept(match);
                    continue;
                }
                if (!placement)
                    throw CodegenException("No room for a jump to the stub of " + substitution.InstructionName + " within its neighbours or a nearby island", fileOffset);
                for (auto& site : placement->Sites)
                    result.Trampolines.push_back(std::move(site));
                claimed.insert(placement->Claimed.begin(), placement->Claimed.end());
                consumed.insert(placement->Consumed.begin(), placement->Consumed.end());
                replacementLength = placement->ReplacementLength;
                break;
            }
            std::vector<std::size_t> taken;
            try {
                std::size_t siteLength = match.Length;
                auto stub = substitution;
                if (siteLength < Amd64OnlySubstitutionTable::kJmpRel32.Size) {
                    const X64InstructionDecoder decoder;
                    std::vector<std::span<const std::uint8_t>> sequence{std::span<const std::uint8_t>(seg.data() + match.Offset, match.Length)};
                    std::size_t trailingBytes = 0;
                    for (auto next = item.Index + 1; siteLength < Amd64OnlySubstitutionTable::kJmpRel32.Size; ++next) {
                        if (next >= matches.size() || matches[next].Offset != match.Offset + siteLength)
                            throw CodegenException("AMD-only instruction too short for a jump and not followed by an instruction", fileOffset);
                        if (claimed.contains(next))
                            throw CodegenException("AMD-only instruction too short for a jump is followed by an instruction another stub already moved", ph.Offset + matches[next].Offset);
                        const auto& following = matches[next];
                        const std::span<const std::uint8_t> bytes(seg.data() + following.Offset, following.Length);
                        const auto info = decoder.DecodeInstruction(bytes.data(), bytes.size());
                        const bool amdOnly = matcherAt(following.Offset).Match(bytes.data(), bytes.size()).has_value();
                        if (amdOnly && trailingBytes == 0) {
                            sequence.push_back(bytes);
                            taken.push_back(next);
                        } else if (amdOnly || info.FlowKind != ControlFlowKind::Sequential || info.HasBranchTarget) {
                            throw CodegenException("AMD-only instruction too short for a jump is followed by an instruction that cannot move", ph.Offset + following.Offset);
                        } else {
                            trailingBytes += following.Length;
                        }
                        siteLength += following.Length;
                    }
                    const std::span<const std::uint8_t> trailing(seg.data() + match.Offset + siteLength - trailingBytes, trailingBytes);
                    auto relocated = _atFileOffset(fileOffset, [&] { return matcherAt(match.Offset).MatchSequence(sequence, trailing); });
                    if (!relocated.has_value() || relocated->Lowering != Amd64OnlyLowering::Trampoline)
                        throw CodegenException("AMD-only instruction sequence has no out-of-line lowering", fileOffset);
                    stub = std::move(*relocated);
                }
                const auto hit = branchTargets.upper_bound(address);
                if (hit != branchTargets.end() && *hit < address + siteLength)
                    throw CodegenException("Branch enters an AMD-only instruction", ph.Offset + (*hit - ph.MappedAddress));
                const auto begin = seg.begin() + static_cast<std::ptrdiff_t>(match.Offset);
                result.Trampolines.push_back({
                    fileOffset,
                    address,
                    siteLength,
                    std::vector<std::uint8_t>(begin, begin + static_cast<std::ptrdiff_t>(siteLength)),
                    stub.StubBody,
                    stub.ReturnBranchOffset,
                    stub.Relocations
                });
                replacementLength = stub.StubBody.size();
                consumed.insert(taken.begin(), taken.end());
                for (auto k = item.Index; k < matches.size() && matches[k].Offset < match.Offset + siteLength; ++k)
                    claimed.insert(k);
            } catch (const CodegenException&) {
                if (!substitution.Optional)
                    throw;
                ++result.KeptCount;
                result.Reports.push_back({substitution.InstructionName, fileOffset, match.Length, 0, Amd64OnlyLowering::Kept});
                countKept(match);
                continue;
            }
            break;
        }
        case Amd64OnlyLowering::Kept:
        case Amd64OnlyLowering::Unsupported:
            throw CodegenException("AMD-only instruction without Intel lowering: " + substitution.InstructionName, fileOffset);
        }

        result.Reports.push_back({substitution.InstructionName, fileOffset, match.Length, replacementLength, substitution.Lowering});
    }

    if (seg.size() != segSize)
        throw CodegenException("Code segment size changed during Intel conversion", ph.Offset);
    std::copy(seg.begin(), seg.end(), fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset));
    if (progress) progress(completed + std::uint64_t{3} * segSize, total);
}

ConvertResult Amd64OnlyConverter::Convert(
    std::vector<std::uint8_t> fileBytes,
    const std::vector<Domain::ProgramHeader>& segments
) const {
    ConvertResult result{};
    std::uint64_t total = 0;
    for (const auto& ph : segments) {
        if (ph.Type != kLoadSegment || (ph.Flags & kExecutableSegment) == 0) continue;
        if (ph.FileSize > (std::numeric_limits<std::uint64_t>::max() - total) / 3) throw CodegenException("Instruction conversion progress size overflow");
        total += 3 * ph.FileSize;
    }
    if (progress) progress(0, total);
    std::uint64_t completed = 0;
    for (const auto& ph : segments) {
        if (ph.Type != kLoadSegment || (ph.Flags & kExecutableSegment) == 0) continue;
        _convertSegment(fileBytes, ph, segments, result, completed, total);
        completed += 3 * ph.FileSize;
    }
    if (progress) progress(total, total);
    result.Bytes = std::move(fileBytes);
    return result;
}

}

std::unique_ptr<IAmd64OnlyConverter> MakeAmd64OnlyConverter(const Amd64OnlyTarget target, std::function<void(std::uint64_t, std::uint64_t)> progress, std::optional<UpperShadowContext> shadow) {
    return std::make_unique<Amd64OnlyConverter>(target, std::move(progress), std::move(shadow));
}

}
