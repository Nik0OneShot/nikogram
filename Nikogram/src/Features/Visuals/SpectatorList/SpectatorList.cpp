#include "SpectatorList.h"
#include "../../ImGui/MoonlitHud.h"
#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"
#include "../../ImGui/Workspace.h"
#include "../../ImGui/Menu/Menu.h"
#include "../../ImGui/Menu/BindLayout.h"

void CSpectatorList::GetSpectators(CTFPlayer* local)
{
    using namespace SpectatorStyle;
    m_vSpectators.clear();
    auto resource = H::Entities.GetResource();
    if (!resource) return;
    const int maxClients = std::min(I::EngineClient->GetMaxClients(), MAX_PLAYERS);
    for (int n = 1; n <= maxClients; ++n)
    {
        if (!resource->m_bValid(n) || !resource->m_bConnected(n)) continue;
        if (resource->m_iTeam(n) < TEAM_SPECTATOR || (resource->m_bAlive(n) && resource->m_iTeam(n) != TEAM_SPECTATOR)) continue;
        auto entity = I::ClientEntityList->GetClientEntity(n);
        auto player = entity ? entity->As<CTFPlayer>() : nullptr;
        const bool known = player && player->IsPlayer() && !player->IsDormant();
        int mode = known ? player->m_iObserverMode() : OBS_MODE_NONE;
        auto target = known ? player->m_hObserverTarget().Get() : nullptr;
        if (known && player == local && F::Spectate.HasTarget())
        {
            mode = F::Spectate.m_iOriginalMode;
            target = F::Spectate.m_hOriginalTarget.Get();
        }
        auto classification = Classify(mode, known);
        if (known && mode == OBS_MODE_NONE && resource->m_iTeam(n) == TEAM_SPECTATOR)
            classification = Classify(mode, false);
        int targetIndex = classification.targeted && target && target->IsPlayer() ? target->entindex() : 0;
        if (targetIndex < 1 || targetIndex > maxClients || !resource->m_bValid(targetIndex) || !resource->m_bConnected(targetIndex)) targetIndex = 0;
        if (Vars::Menu::SpectatorScope.Value == 1 && targetIndex != local->entindex()) continue;
        if (!Include(classification.state, classification.view, Vars::Menu::SpectatorStates.Value, Vars::Menu::SpectatorViews.Value)) continue;
        int respawn = -1;
        if (resource->m_iTeam(n) > TEAM_SPECTATOR)
        {
            const float seconds = resource->m_flNextRespawnTime(n) - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick);
            if (std::isfinite(seconds) && seconds > 0.f && seconds < 3600.f) respawn = int(std::ceil(seconds));
        }
        m_vSpectators.push_back({ F::PlayerUtils.GetPlayerName(n, resource->GetName(n)),
            targetIndex ? F::PlayerUtils.GetPlayerName(targetIndex, resource->GetName(targetIndex)) : "UNASSIGNED",
            classification.state, classification.view, targetIndex, respawn });
    }
    std::stable_sort(m_vSpectators.begin(), m_vSpectators.end(), [](const auto& a, const auto& b)
    {
        if (Vars::Menu::SpectatorGroup.Value && a.targetIndex != b.targetIndex)
        {
            if (!a.targetIndex || !b.targetIndex) return a.targetIndex != 0;
            if (a.target != b.target) return a.target < b.target;
            return a.targetIndex < b.targetIndex;
        }
        return a.name < b.name;
    });
}

void CSpectatorList::DrawMoonlit(CTFPlayer* local)
{
    using namespace MoonlitHud;
    const int pad = MoonlitHud::S(14), gap = MoonlitHud::S(8), line = Detail().m_nTall + gap;
    const bool targets = Vars::Menu::SpectatorTargets.Value, labels = Vars::Menu::SpectatorLabels.Value;
    const bool grouped = Vars::Menu::SpectatorGroup.Value && targets;
    const bool horizontal = Vars::Menu::SpectatorLayout.Value == 1;
    const bool respawn = Vars::Menu::SpectatorRespawn.Value;
    const int screenW = H::Draw.m_nScreenW, screenH = H::Draw.m_nScreenH;
    if (screenW < MoonlitHud::S(200) || screenH < MoonlitHud::S(120)) return;
    const int minW = std::min(MoonlitHud::S(260), screenW);
    int entriesAcross = std::max(1, int(m_vSpectators.size()));
    if (grouped)
    {
        int run = 0, previous = -1; entriesAcross = 1;
        for (const auto& entry : m_vSpectators)
        {
            run = entry.targetIndex == previous ? run + 1 : 1; previous = entry.targetIndex;
            entriesAcross = std::max(entriesAcross, run);
        }
    }
    const auto autoSize = SpectatorStyle::SizeHorizontal(std::min(screenW, MoonlitHud::S(650)) - pad * 2,
        MoonlitHud::S(280), gap, entriesAcross, minW - pad * 2);
    const int width = std::clamp(Vars::Menu::SpectatorWidth.Value > 0 ? Vars::Menu::SpectatorWidth.Value
        : (horizontal ? autoSize.width + pad * 2 : MoonlitHud::S(310)), minW, screenW);
    const int inner = width - pad * 2;
    const int columns = horizontal ? std::min(entriesAcross, SpectatorStyle::Columns(inner, MoonlitHud::S(280), gap)) : 1;
    const int card = (inner - (columns - 1) * gap) / columns;
    const int rowHeight = pad * 2 + Label().m_nTall + (targets && !grouped ? line : 0) + (labels || respawn ? line : 0);
    const int startY = pad + Label().m_nTall + gap * 2;
    const int minH = startY + (grouped ? line : 0) + rowHeight + line + pad;
    m_vMinimumSize = {float(minW), float(std::min(minH, screenH))};
    if (screenH < minH) return;
    const int maxH = std::clamp(Vars::Menu::SpectatorHeight.Value > 0 ? Vars::Menu::SpectatorHeight.Value : MoonlitHud::S(600), minH, screenH);
    std::vector<int> targetIndices;
    for (const auto& entry : m_vSpectators) targetIndices.push_back(entry.targetIndex);
    const auto pages = SpectatorStyle::Paginate(targetIndices, grouped, columns, card, gap, pad, line, rowHeight, startY, maxH);
    m_iPageCount = int(pages.size());
    m_iCurrentPage = std::clamp(Vars::Menu::SpectatorPage.Value, 1, m_iPageCount);
    const auto& placements = pages[m_iCurrentPage - 1];
    int bottom = startY + line;
    for (const auto& place : placements) bottom = std::max(bottom, place.y + (place.header ? line : rowHeight));
    const int height = Vars::Menu::SpectatorHeight.Value > 0 ? maxH : std::max(minH, bottom + line + pad);
    const auto position = Vars::Menu::SpectatorsDisplay.Value;
    const auto box = Bounds(position.x, position.y, width, height);
    m_vIndicatorSize = {float(box.w), float(box.h)};
    Frame(box);
    H::Draw.StartClipping(box.x, box.y, box.w, box.h);
    Badge(box.x + pad, box.y + pad - MoonlitHud::S(3), MoonlitHud::S(20));
    Text(box.x + pad + BadgeSpace(), box.y + pad, "spectators", Ink, Label(), inner / 2 - BadgeSpace());
    Text(box.x + width - pad, box.y + pad + MoonlitHud::S(1), Vars::Menu::SpectatorScope.Value == 1 ? "watching me" : "all players",
        Muted, Detail(), inner / 2, ALIGN_TOPRIGHT);
    if (m_vSpectators.empty()) Text(box.x + pad, box.y + startY, "no matching spectators", Muted, Detail(), inner);
    for (const auto& place : placements)
    {
        const auto& entry = m_vSpectators[place.index];
        const auto accent = SpectatorStyle::WatchingLocal(entry.targetIndex, local->entindex()) ? Gold : Lavender;
        const int x = box.x + place.x, y = box.y + place.y;
        if (place.header) { Text(x, y, "watching " + entry.target, accent, Detail(), inner); continue; }
        H::Draw.FillRoundRect(x, y, card, rowHeight, MoonlitHud::S(8), Raised);
        Text(x + pad, y + pad, entry.name, Ink, Label(), card - pad * 2);
        int detailY = y + pad + Label().m_nTall + gap;
        if (targets && !grouped)
        {
            Text(x + pad, detailY, "watching " + entry.target, accent, Detail(), card - pad * 2);
            detailY += line;
        }
        std::string state = labels ? std::string(SpectatorStyle::Label(entry.state)) + " / " +
            (entry.view == SpectatorStyle::DeathCamera ? (entry.state == SpectatorStyle::Freeze ? "FREEZE CAM" : "DEATH CAM") : SpectatorStyle::Label(entry.view)) : "";
        const std::string timer = respawn && entry.respawn >= 0 ? std::format("{}s", entry.respawn) : "";
        const int timerW = Measure(timer, Detail());
        Text(x + pad, detailY, state, Muted, Detail(), card - pad * 2 - timerW - (timer.empty() ? 0 : gap));
        if (!timer.empty()) Text(x + card - pad, detailY, timer, Gold, Detail(), timerW, ALIGN_TOPRIGHT);
    }
    Row(box, box.y + height - pad - Detail().m_nTall, std::format("{} observers", m_iEntries),
        std::format("page {} / {}", m_iCurrentPage, m_iPageCount));
    H::Draw.EndClipping();
}

void CSpectatorList::Draw(CTFPlayer* local)
{
    if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Spectators) || !local) return;
    GetSpectators(local);
    m_iEntries = int(m_vSpectators.size());
    if (m_vSpectators.empty()) m_iCurrentPage = m_iPageCount = 1;
    if (m_vSpectators.empty() && !F::Menu.m_bIsOpen) return;
    if (MoonlitHud::Enabled()) { DrawMoonlit(local); return; }
    const auto& font = H::Fonts.GetFont(FONT_CRIT_LABEL);
    const int pad = std::max(3, int(H::Draw.Scale(6, Scale_Round)));
    const int gap = std::max(2, int(H::Draw.Scale(4, Scale_Round)));
    const int line = font.m_nTall + gap;
    const bool horizontal = Vars::Menu::SpectatorLayout.Value == 1;
    const bool targets = Vars::Menu::SpectatorTargets.Value;
    const bool labels = Vars::Menu::SpectatorLabels.Value;
    const bool grouped = Vars::Menu::SpectatorGroup.Value && targets;
    const int taskbar = F::Menu.m_bIsOpen ? int(H::Draw.Scale(26)) : 0;
    const int screenW = H::Draw.m_nScreenW, availableH = H::Draw.m_nScreenH - taskbar;
    if (screenW < 200 || availableH < 100) return;
    const int minWidth = std::min(screenW, int(H::Draw.Scale(200)));
    const int customWidth = Vars::Menu::SpectatorWidth.Value > 0 ? std::clamp(Vars::Menu::SpectatorWidth.Value, minWidth, screenW) : 0;
    auto measure = [&](std::string value)
    {
        for (auto& c : value) if (static_cast<unsigned char>(c) < 32) c = ' ';
        return int(std::ceil(H::Draw.GetTextSize(value.c_str(), font).x));
    };
    const char* title = Vars::Menu::SpectatorScope.Value == 1 ? "SPECTATORS / WATCHING ME" : "SPECTATORS / ALL PLAYERS";
    auto compactName = [&](std::string name)
    {
        const int limit = int(H::Draw.Scale(120));
        if (measure(name) <= limit) return name;
        while (!name.empty() && measure(name + "...") > limit)
        {
            size_t pos = name.size() - 1;
            while (pos && (static_cast<unsigned char>(name[pos]) & 0xc0) == 0x80) --pos;
            name.resize(pos);
        }
        return name + "...";
    };
    auto cardLines = [&](const Spectator_t& entry)
    {
        std::string first = compactName(entry.name), second;
        if (targets) first += " > " + (entry.targetIndex ? compactName(entry.target) : "UNASSIGNED");
        if (labels)
        {
            second = std::string(SpectatorStyle::Label(entry.state)) + " | ";
            second += entry.view == SpectatorStyle::DeathCamera
                ? (entry.state == SpectatorStyle::Freeze ? "FREEZE CAM" : "DEATH CAM") : SpectatorStyle::Label(entry.view);
        }
        if (Vars::Menu::SpectatorRespawn.Value && entry.respawn >= 0)
        {
            if (!second.empty()) second += " | ";
            second += std::format("RESPAWN {}s", entry.respawn);
        }
        return std::pair(first, second);
    };
    std::array<int, 4> measured = { measure("OBSERVER"), targets ? measure("TARGET") : 0,
        labels ? measure("STATE") : 0, labels ? measure("VIEW") : 0 };
    if (!horizontal)
    {
        const int nameCap = std::max(measure("OBSERVER"), int(H::Draw.Scale(120)));
        // Measure the filtered list, not only the current page, to avoid resizing
        // on page changes. Long player names cannot enlarge the panel indefinitely.
        for (const auto& entry : m_vSpectators)
        {
            std::string name = entry.name;
            if (Vars::Menu::SpectatorRespawn.Value && entry.respawn >= 0) name += std::format(" ({}s)", entry.respawn);
            measured[0] = std::max(measured[0], std::min(nameCap, measure(name)));
            if (targets) measured[1] = std::max(measured[1], std::min(nameCap, measure(entry.target)));
            if (labels)
            {
                measured[2] = std::max(measured[2], measure(SpectatorStyle::Label(entry.state)));
                measured[3] = std::max(measured[3], measure(SpectatorStyle::Label(entry.view)));
            }
        }
    }
    for (int& w : measured) if (w) w += gap * 2;
    measured = SpectatorStyle::FitColumns(measured, screenW - pad * 2);
    int minimumInner = measure(title);
    if (m_vSpectators.empty()) minimumInner = std::max(minimumInner, measure("NO MATCHING SPECTATORS"));
    int cardWidth = 0, largestGroup = 0, run = 0, previousTarget = -1;
    bool hasDetails = false;
    if (horizontal) for (const auto& entry : m_vSpectators)
    {
        const auto [first, second] = cardLines(entry);
        cardWidth = std::max(cardWidth, std::max(measure(first), measure(second)) + pad * 2);
        hasDetails = hasDetails || !second.empty();
        run = entry.targetIndex == previousTarget ? run + 1 : 1;
        largestGroup = std::max(largestGroup, run); previousTarget = entry.targetIndex;
    }
    const int horizontalAvailable = (customWidth ? customWidth : std::min(screenW, int(H::Draw.Scale(720)))) - pad * 2;
    cardWidth = std::min(horizontalAvailable, std::max(cardWidth, minimumInner));
    const auto horizontalSize = SpectatorStyle::SizeHorizontal(horizontalAvailable, cardWidth, gap,
        grouped ? largestGroup : int(m_vSpectators.size()), minimumInner);
    const int width = customWidth ? customWidth : std::max(minWidth, std::min(screenW, (horizontal ? horizontalSize.width
        : std::max(minimumInner, measured[0] + measured[1] + measured[2] + measured[3])) + pad * 2));
    const int inner = width - 2 * pad;
    measured = SpectatorStyle::FitColumns(measured, inner);
    if (customWidth)
    {
        const int spare = std::max(0, inner - measured[0] - measured[1] - measured[2] - measured[3]);
        measured[0] += targets ? spare / 2 : spare;
        if (targets) measured[1] += spare - spare / 2;
    }
    const int nameW = measured[0], targetW = measured[1], stateW = measured[2], viewW = measured[3];
    const int card = horizontal ? std::min(inner, cardWidth) : inner;
    const int columns = horizontal ? horizontalSize.columns : 1;
    const int rowHeight = horizontal ? pad * 2 + line * (hasDetails ? 2 : 1) : line + pad;
    const int startY = pad + line * (horizontal ? 1 : 2);
    const int minHeight = startY + rowHeight + (grouped ? line : 0) + pad;
    m_vMinimumSize = { float(minWidth), float(std::min(minHeight, availableH)) };
    if (availableH < minHeight) return;
    const int maxHeight = Vars::Menu::SpectatorHeight.Value > 0
        ? std::clamp(Vars::Menu::SpectatorHeight.Value, minHeight, availableH)
        : std::min(availableH, std::max(minHeight, int(H::Draw.Scale(600))));
    std::vector<int> targetIndices;
    for (const auto& entry : m_vSpectators) targetIndices.push_back(entry.targetIndex);
    const auto pages = SpectatorStyle::Paginate(targetIndices, grouped, columns, card, gap, pad, line, rowHeight, startY, maxHeight, false);
    const int page = std::clamp(Vars::Menu::SpectatorPage.Value - 1, 0, int(pages.size()) - 1);
    m_iPageCount = int(pages.size()); m_iCurrentPage = page + 1;
    int bottom = startY + line;
    for (const auto& placement : pages[page]) bottom = std::max(bottom, placement.y + (placement.header ? line : rowHeight));
    const int height = Vars::Menu::SpectatorHeight.Value > 0 ? maxHeight : std::max(minHeight, bottom + pad);
    m_vIndicatorSize = { float(width), float(height) };
    const int left = int(BindLayout::ClampAxis(float(Vars::Menu::SpectatorsDisplay.Value.x - width / 2), float(width), 0.f, float(screenW)));
    const int top = int(BindLayout::ClampAxis(float(Vars::Menu::SpectatorsDisplay.Value.y), float(height), Workspace::TopTaskbar ? float(taskbar) : 0.f, float(H::Draw.m_nScreenH - (Workspace::TopTaskbar ? 0 : taskbar))));
    const Color_t accent(int(Workspace::Accent[0] * 255), int(Workspace::Accent[1] * 255), int(Workspace::Accent[2] * 255), 255);
    const Color_t border(int(Workspace::BorderChannel(0) * 255), int(Workspace::BorderChannel(1) * 255), int(Workspace::BorderChannel(2) * 255), 255);
    auto text = [&](int x, int ty, std::string value, int maxWidth, bool watchingLocal = false)
    {
        if (maxWidth <= 0) return;
        for (auto& c : value) if (static_cast<unsigned char>(c) < 32) c = ' ';
        if (H::Draw.GetTextSize(value.c_str(), font).x > maxWidth)
        {
            while (!value.empty() && H::Draw.GetTextSize((value + "...").c_str(), font).x > maxWidth)
            {
                size_t pos = value.size() - 1;
                while (pos && (static_cast<unsigned char>(value[pos]) & 0xc0) == 0x80) --pos;
                value.resize(pos);
            }
            if (measure("...") > maxWidth) return;
            value += "...";
        }
        Color_t foreground = accent;
        if (watchingLocal)
        {
            // Keep a crisp centre with a restrained one-pixel halo, not a solid blur.
            foreground = Color_t(accent.r + (255 - accent.r) * .35f,
                accent.g + (255 - accent.g) * .35f, accent.b + (255 - accent.b) * .35f, 255);
            const Color_t halo(foreground.r, foreground.g, foreground.b, 32);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy)
                        H::Draw.String(font, left + x + dx, top + ty + dy, halo, ALIGN_TOPLEFT, value.c_str());
        }
        H::Draw.String(font, left + x, top + ty, foreground, ALIGN_TOPLEFT, value.c_str());
    };
    H::Draw.FillRect(left, top, width, height, { 0, 0, 0, 255 });
    H::Draw.LineRect(left, top, width, height, border);
    text(pad, pad, title, inner);
    if (!horizontal)
    {
        int columnX = pad;
        const char* headings[] = { "OBSERVER", "TARGET", "STATE", "VIEW" };
        for (int i = 0; i < 4; ++i)
        {
            const int cellWidth = measured[i];
            if (!cellWidth) continue;
            H::Draw.LineRect(left + columnX, top + pad + line, cellWidth, line, border);
            text(columnX + gap, pad + line + gap / 2, headings[i], cellWidth - gap * 2);
            columnX += cellWidth;
        }
    }
    for (const auto& placement : pages[page])
    {
        const auto& entry = m_vSpectators[placement.index];
        const bool watchingLocal = SpectatorStyle::WatchingLocal(entry.targetIndex, local->entindex());
        if (placement.header)
        {
            const int watchers = int(std::count(targetIndices.begin(), targetIndices.end(), entry.targetIndex));
            text(pad, placement.y, "TARGET / " + entry.target + std::format(" / {} ENTRIES", watchers), inner, watchingLocal);
            continue;
        }
        const int px = placement.x, py = placement.y, w = horizontal ? card : inner;
        if (horizontal) H::Draw.LineRect(left + px, top + py, w, rowHeight, border);
        std::string name = entry.name;
        if (!horizontal && Vars::Menu::SpectatorRespawn.Value && entry.respawn >= 0) name += std::format(" ({}s)", entry.respawn);
        if (horizontal)
        {
            const auto [first, second] = cardLines(entry);
            text(px + pad, py + pad, first, w - 2 * pad, watchingLocal);
            if (!second.empty()) text(px + pad, py + pad + line, second, w - 2 * pad, watchingLocal);
        }
        else
        {
            int columnX = px;
            for (const int cellWidth : measured)
            {
                if (!cellWidth) continue;
                H::Draw.LineRect(left + columnX, top + py, cellWidth, rowHeight, border);
                columnX += cellWidth;
            }
            text(px + gap, py + pad / 2, name, nameW - gap * 2, watchingLocal);
            if (targets) text(px + nameW + gap, py + pad / 2, entry.target, targetW - gap * 2, watchingLocal);
            if (labels) { text(px + nameW + targetW + gap, py + pad / 2, SpectatorStyle::Label(entry.state), stateW - gap * 2, watchingLocal); text(px + nameW + targetW + stateW + gap, py + pad / 2, SpectatorStyle::Label(entry.view), viewW - gap * 2, watchingLocal); }
        }
    }
    if (m_vSpectators.empty()) text(pad, startY, "NO MATCHING SPECTATORS", inner);
}
