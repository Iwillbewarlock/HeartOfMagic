#include "Common.h"

#include "BlockDraw.h"

#include "Addon.h"
#include "Draw.h"
#include "Lang.h"
#include "SRPrismaAPI.h"
#include "Settings.h"
#include "SrBlockRules.h"
#include "SrData.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace BlockDraw
{
    namespace
    {
        // SrBlockRules.h keeps SR Prisma's numbers without its header (the check tool has no SKSE): they must agree
        static_assert(SrBlock::kLinks[0].what == static_cast<std::uint32_t>(SRPrismaAPI::What::kJournal));
        static_assert(SrBlock::kLinks[1].what == static_cast<std::uint32_t>(SRPrismaAPI::What::kAlembic));
        static_assert(SrBlock::kLinks[2].what == static_cast<std::uint32_t>(SRPrismaAPI::What::kExperience));
        static_assert(SrBlock::kResultAccepted == static_cast<std::uint32_t>(SRPrismaAPI::Result::kAccepted));
        static_assert(SrBlock::kResultBusy == static_cast<std::uint32_t>(SRPrismaAPI::Result::kBusy));
        static_assert(SrBlock::kResultRefused == static_cast<std::uint32_t>(SRPrismaAPI::Result::kRefused));
        static_assert(SrBlock::kResultBadMessage == static_cast<std::uint32_t>(SRPrismaAPI::Result::kBadMessage));
        static_assert(SrBlock::kResultOpened == static_cast<std::uint32_t>(SRPrismaAPI::Result::kOpened));

        // SL_MagicMenu Widget.cpp's colours: names, dim text (group labels, "?", separators, the reason line), the icons, the
        // links' text and underline
        constexpr std::uint32_t kWhite = 0xF0F0F0, kDimText = 0x8A8A8A, kTitleColor = 0xC8C0B0, kLabelColor = 0xD8D8D8,
                                kPanelEdge = 0x8A7A5A;
        constexpr double kRowSize = 11.0, kLinkSize = 10.0, kReasonSize = 10.0;
        // Widget.cpp's text line height per size unit before it measures the font (kLineFactor) and the 2 units an AS2 text
        // field keeps free on each side of its text (textWidth leaves them out: kGutter = both)
        constexpr double kLineFactor = 1.25, kGutter = 4.0, kGutterSide = 2.0;
        constexpr double kIconSize = 14.0, kIconGap = 3.0, kSeparatorPad = 4.0;
        constexpr double kUnderlineW = 1.0, kUnderlineDrop = 1.0;
        constexpr int kDotSegments = 8;     // a dot subpath drawn as a small filled polygon of this many sides
        constexpr double kMiterLimit = 3.0;  // lineStyle's miterLimit (unused with round joins; AS2 wants the argument)
        constexpr double kFitSlack = 0.5;
        constexpr const char* kSeparator = "\xC2\xB7";  // U+00B7, which the game font has (Widget.cpp, build 3 frame f_008)
        constexpr int kMaxDrawnLogs = 16;  // "[selftest-sr] drawn" lines per menu
        constexpr const char* kListSelectionFormId = "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId";  // SkyUI

        // The links drawn last: written by Draw, read by Click - both on the UI thread (SL_MagicMenu's frame handler), so no lock
        std::vector<SrBlock::Rect> g_linkRects;
        std::atomic<bool> g_noResponse{ false };  // the last open request was not delivered: the reason line says so
        std::atomic<int> g_drawnLogs{ 0 };
        SrBlock::PendingRequest g_pending;
        std::atomic<std::uint32_t> g_nextRequestId{ 1 };

        std::int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        double FieldH(double a_size)
        {
            return a_size * kLineFactor + kGutter;
        }

        std::optional<double> Number(const RE::GFxValue& a_value, const char* a_name)
        {
            RE::GFxValue member;
            if (!a_value.GetMember(a_name, &member) || !member.IsNumber()) return std::nullopt;
            return member.GetNumber();
        }

        double TextW(const RE::GFxValue& a_field)
        {
            return Number(a_field, "textWidth").value_or(0.0);
        }

        void SetText(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, const std::string& a_text)
        {
            RE::GFxValue text;
            a_movie->CreateString(&text, a_text.c_str());
            a_field.SetMember("text", text);
        }

        std::size_t CodePoints(std::string_view a_text)
        {
            return static_cast<std::size_t>(std::ranges::count_if(a_text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        }

        // The text cut to the most code points that fit a_maxW with "..." (Draw::Cut), by bisection on the laid-out width
        void FitTo(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, const std::string& a_text, double a_maxW)
        {
            SetText(a_movie, a_field, a_text);
            if (TextW(a_field) <= a_maxW + kFitSlack) return;
            std::size_t lo = 0, hi = CodePoints(a_text);
            while (lo < hi) {
                const std::size_t mid = (lo + hi) / 2;
                SetText(a_movie, a_field, Draw::Cut(a_text, mid));
                if (TextW(a_field) <= a_maxW + kFitSlack) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            SetText(a_movie, a_field, Draw::Cut(a_text, lo > 0 ? lo - 1 : 0));
        }

        // Text fields in the content clip, each with its own name
        struct Texts
        {
            RE::GFxMovieView* movie;
            RE::GFxValue& clip;
            double width;
            int count = 0;

            RE::GFxValue Make(const std::string& a_text, std::uint32_t a_rgb, double a_size)
            {
                const auto name = std::format("t{}", count++);
                return Draw::Text(movie, clip, name.c_str(), 0, 0, width, FieldH(a_size), a_text, a_rgb, a_size);
            }
        };

        // The field's text starts at a_x (the field itself 2 units left of it) and is a_w wide
        void Put(RE::GFxValue& a_field, double a_x, double a_y, double a_w)
        {
            a_field.SetMember("_x", a_x - kGutterSide);
            a_field.SetMember("_y", a_y);
            a_field.SetMember("_width", a_w + kGutter);
        }

        void Hide(RE::GFxValue& a_field)
        {
            a_field.SetMember("_visible", false);
        }

        double RowTop(int a_line) { return a_line * SrBlock::kRowH; }
        double TextY(int a_line, double a_size) { return RowTop(a_line) + (SrBlock::kRowH - FieldH(a_size)) / 2; }
        double IconY(int a_line) { return RowTop(a_line) + (SrBlock::kRowH - kIconSize) / 2; }

        // An archetype's icon in a kIconSize box at (a_x, a_y): ArchetypeIcons' polylines scaled from the 24-unit grid, strokes
        // kIconStroke wide (scaled too) with round caps and joins; an unknown archetype draws its group's frame only
        void DrawIcon(RE::GFxValue& a_clip, int a_archetype, bool a_known, double a_x, double a_y)
        {
            using namespace ArchetypeIcons;
            const auto range = SrBlock::IconSubpaths(a_archetype, a_known);
            const double scale = kIconSize / kIconGrid, stroke = kIconStroke * scale;
            auto at = [&](const Point& a_p) { return std::pair{ a_x + a_p.x * scale, a_y + a_p.y * scale }; };
            for (std::size_t k = range.first; k < range.first + range.count; ++k) {
                const auto& sub = kSubpaths[k];
                const Point* points = &kPoints[sub.first];
                const auto [x0, y0] = at(points[0]);
                const bool dot = sub.count == 2 && points[0].x == points[1].x && points[0].y == points[1].y;
                if (sub.fill || dot) {
                    // a zero-length stroke draws nothing in AS2: a dot is a small filled polygon, as wide as the stroke
                    Draw::NoLine(a_clip);
                    Draw::BeginFill(a_clip, kTitleColor, Draw::kAlphaFull);
                    if (dot) {
                        const double r = stroke / 2;
                        Draw::MoveTo(a_clip, x0 + r, y0);
                        for (int i = 1; i <= kDotSegments; ++i) {
                            const double t = 2 * std::numbers::pi * i / kDotSegments;
                            Draw::LineTo(a_clip, x0 + r * std::cos(t), y0 + r * std::sin(t));
                        }
                    } else {
                        Draw::MoveTo(a_clip, x0, y0);
                        for (std::size_t i = 1; i < sub.count; ++i) {
                            const auto [x, y] = at(points[i]);
                            Draw::LineTo(a_clip, x, y);
                        }
                        Draw::LineTo(a_clip, x0, y0);
                    }
                    Draw::EndFill(a_clip);
                    continue;
                }
                // Flash 8 lineStyle(thickness, rgb, alpha, pixelHinting, noScale, capsStyle, jointStyle, miterLimit)
                Draw::Call(a_clip, "lineStyle", { stroke, static_cast<double>(kTitleColor), Draw::kAlphaFull, false, "normal", "round", "round", kMiterLimit });
                Draw::MoveTo(a_clip, x0, y0);
                for (std::size_t i = 1; i < sub.count; ++i) {
                    const auto [x, y] = at(points[i]);
                    Draw::LineTo(a_clip, x, y);
                }
                if (sub.closed) Draw::LineTo(a_clip, x0, y0);
            }
        }

        // The archetypes, laid out by the layout SR Prisma's settings chose (list or line)
        void DrawItems(Texts& a_texts, const std::vector<SrBlock::Item>& a_items)
        {
            const auto kind = Settings::CurrentLayout();
            std::vector<RE::GFxValue> names;
            std::vector<std::string> nameTexts;
            std::vector<SrBlock::Measured> measured;
            for (const auto& item : a_items) {
                nameTexts.push_back(item.known ? Lang::Get(std::format("archetype.{}", item.archetype)) : Lang::Get("unknown"));
                names.push_back(a_texts.Make(nameTexts.back(), item.known ? kWhite : kDimText, kRowSize));
                measured.push_back({ item, TextW(names.back()) });
                Hide(names.back());
            }
            std::array<RE::GFxValue, SrBlock::kShownGroups.size()> labels;
            std::array<std::string, SrBlock::kShownGroups.size()> labelTexts;
            std::array<double, SrBlock::kShownGroups.size()> labelW{};
            if (kind == SrBlock::LayoutKind::kList) {
                for (std::size_t g = 0; g < labels.size(); ++g) {
                    labelTexts[g] = Lang::Get(SrBlock::GroupKey(SrBlock::kShownGroups[g]));
                    labels[g] = a_texts.Make(labelTexts[g], kDimText, kRowSize);
                    labelW[g] = TextW(labels[g]);
                    Hide(labels[g]);
                }
            }
            auto probe = a_texts.Make(kSeparator, kDimText, kRowSize);
            const double separatorW = TextW(probe);
            Hide(probe);
            const auto moreText = Lang::Get("more");
            auto more = a_texts.Make(moreText, kDimText, kRowSize);
            const double moreW = TextW(more);
            Hide(more);

            const SrBlock::Metrics metrics{ .width = a_texts.width, .iconSize = kIconSize, .iconGap = kIconGap, .separatorW = separatorW,
                .separatorPad = kSeparatorPad, .ellipsisW = moreW };
            const auto placement = SrBlock::Place(kind, metrics, measured, labelW);
            for (const auto& part : placement.parts) {
                using Kind = SrBlock::Part::Kind;
                const double y = TextY(part.line, kRowSize);
                switch (part.kind) {
                case Kind::kLabel: {
                    auto& label = labels[static_cast<std::size_t>(part.index)];
                    Put(label, part.x, y, part.w);
                    if (part.w < labelW[static_cast<std::size_t>(part.index)]) FitTo(a_texts.movie, label, labelTexts[static_cast<std::size_t>(part.index)], part.w);
                    label.SetMember("_visible", true);
                    break;
                }
                case Kind::kIcon: DrawIcon(a_texts.clip, part.index, part.known, part.x, IconY(part.line)); break;
                case Kind::kText: {
                    const auto it = std::ranges::find_if(measured, [&](const SrBlock::Measured& a_m) { return a_m.item.archetype == part.index; });
                    const auto i = static_cast<std::size_t>(it - measured.begin());
                    if (i >= names.size()) break;
                    Put(names[i], part.x, y, part.w);
                    if (part.w < measured[i].textW) FitTo(a_texts.movie, names[i], nameTexts[i], part.w);
                    names[i].SetMember("_visible", true);
                    break;
                }
                case Kind::kSeparator: {
                    auto dot = a_texts.Make(kSeparator, kDimText, kRowSize);
                    Put(dot, part.x, y, part.w);
                    break;
                }
                case Kind::kEllipsis:
                    Put(more, part.x, y, part.w);
                    more.SetMember("_visible", true);
                    break;
                }
            }
        }

        // The links to SR Prisma's windows, underlined; their rects kept for Click
        void DrawLinks(Texts& a_texts)
        {
            std::vector<RE::GFxValue> fields;
            std::vector<double> widths;
            for (const auto& link : SrBlock::kLinks) {
                fields.push_back(a_texts.Make(Lang::Get(link.key), kLabelColor, kLinkSize));
                widths.push_back(TextW(fields.back()));
            }
            g_linkRects = SrBlock::PlaceLinks(widths, SrBlock::kLinksTop, a_texts.width);
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i >= g_linkRects.size()) {
                    Hide(fields[i]);
                    continue;
                }
                const auto& rect = g_linkRects[i];
                const double w = rect.right - rect.left, y = rect.top + (SrBlock::kLinkRowH - FieldH(kLinkSize)) / 2;
                Put(fields[i], rect.left, y, w);
                if (w < widths[i]) FitTo(a_texts.movie, fields[i], Lang::Get(SrBlock::kLinks[i].key), w);
                const double textH = Number(fields[i], "textHeight").value_or(kLinkSize);
                const double underline = std::min(y + kGutterSide + textH + kUnderlineDrop, rect.bottom);
                Draw::LineStyle(a_texts.clip, kUnderlineW, kPanelEdge);
                Draw::MoveTo(a_texts.clip, rect.left, underline);
                Draw::LineTo(a_texts.clip, rect.left + std::min(w, TextW(fields[i])), underline);
            }
        }

        // Self-test: the form drawn for, against the list's own selection (SkyUI's selectedEntry). A tree click can put another
        // spell on the card than the list's, so a difference is a SKIP, not a FAIL
        void SelfTestDrawn(RE::GFxMovieView* a_movie, std::uint32_t a_shown, const SrBlock::Decision& a_decision)
        {
            if (!Settings::SelfTest() || g_drawnLogs.fetch_add(1) >= kMaxDrawnLogs) return;
            RE::GFxValue selection;
            const bool have = a_movie->GetVariable(&selection, kListSelectionFormId) && selection.IsNumber();
            // via int64 like the host's ToFormId: a number past 0x7FFFFFFF may come as a negative one
            const auto listId = have ? static_cast<std::uint32_t>(static_cast<std::int64_t>(selection.GetNumber())) : 0u;
            const auto shown = std::format("drawn for {:08X} ({}{}{})", a_shown, a_decision.show == SrBlock::Show::kItems ? "archetypes" : "reason",
                *a_decision.reasonKey ? ", " : "", a_decision.reasonKey);
            if (!have) {
                logger::info("[selftest-sr] drawn SKIP {} - the list's selection is not readable", shown);
            } else if (listId == a_shown) {
                logger::info("[selftest-sr] drawn PASS {} = the list's selection", shown);
            } else {
                logger::info("[selftest-sr] drawn SKIP {} - the list's selection is {:08X} (a tree click can put another spell on the card)", shown, listId);
            }
        }

        void HandleReply(const SRPrismaAPI::ReplyMessage& a_reply)
        {
            if (!g_pending.Accept(a_reply.requestId, NowMs())) {
                logger::info("BlockDraw: SR Prisma's reply to request {} came after {} ms - dropped", a_reply.requestId, SrBlock::PendingRequest::kTimeoutMs);
                return;
            }
            const char* key = SrBlock::NotificationKey(a_reply.result);
            logger::info("BlockDraw: SR Prisma answered request {} (what {}) with result {}{}", a_reply.requestId, a_reply.what, a_reply.result,
                key ? std::format(" - notification {}", key) : "");
            if (key) {
                RE::SendHUDMessage::ShowHUDMessage(Lang::Get(key).c_str());
            } else if (a_reply.result != SrBlock::kResultAccepted && a_reply.result != SrBlock::kResultOpened) {
                logger::warn("BlockDraw: SR Prisma's result {} is not one this build knows - no notification", a_reply.result);
            }
            Addon::RequestRedraw();
        }

        void Open(const SrBlock::Link& a_link)
        {
            const auto now = NowMs();
            if (g_pending.Busy(now)) {
                logger::info("BlockDraw: request {} still waits for SR Prisma's reply - the {} link is ignored", g_pending.Id(), a_link.key);
                return;
            }
            auto id = g_nextRequestId.fetch_add(1);
            if (id == 0) id = g_nextRequestId.fetch_add(1);  // 0 is "could not be read" in SR Prisma's reply
            if (!g_pending.Begin(id, now)) return;
            SRPrismaAPI::OpenMessage message{ SRPrismaAPI::kOpenVersion, id, a_link.what };
            auto* messaging = SKSE::GetMessagingInterface();
            const bool delivered = messaging && messaging->Dispatch(SRPrismaAPI::kMessageOpen, &message,
                                                    sizeof(message), SRPrismaAPI::kPluginName);
            if (!delivered) {
                g_pending.Clear(id);
                g_noResponse = true;
                logger::warn("BlockDraw: open request {} ({}) was not delivered - SR Prisma does not listen; the menu stays open", id, a_link.key);
                Addon::RequestRedraw();
                return;
            }
            g_noResponse = false;
            logger::info("BlockDraw: open request {} ({}, what {}) delivered - closing the magic menu", id, a_link.key, a_link.what);
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::MagicMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }
    }

    void Draw(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float, std::uint32_t a_shown, void*)
    {
        g_linkRects.clear();
        if (!a_movie) return;
        const auto snap = SrData::Current();
        auto* form = a_shown ? RE::TESForm::LookupByID(a_shown) : nullptr;
        const auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
        SrBlock::View view{ .shown = a_shown,
            .haveSnapshot = snap != nullptr,
            .ready = snap && snap->ready,
            .listsEmpty = snap && snap->listsEmpty,
            .isSpell = spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell };
        if (snap && snap->ready) {
            if (const auto it = snap->spells.find(a_shown); it != snap->spells.end()) view.entry = &it->second;
            view.knowledge = &snap->knowledge;
        }
        const auto decision = SrBlock::Decide(view);
        if (decision.show == SrBlock::Show::kNothing) return;

        Texts texts{ a_movie, a_clip, static_cast<double>(a_width) };
        if (decision.show == SrBlock::Show::kItems) {
            DrawItems(texts, decision.items);
        } else {
            auto unknown = texts.Make(Lang::Get("unknown"), kDimText, kRowSize);
            Put(unknown, 0, TextY(0, kRowSize), TextW(unknown));
        }
        if (Addon::SrPrismaUsable()) DrawLinks(texts);
        const std::string reasonKey = g_noResponse ? "reason.noResponse" : decision.reasonKey;
        if (!reasonKey.empty()) {
            auto reason = texts.Make("", kDimText, kReasonSize);
            Put(reason, 0, SrBlock::kReasonTop + (SrBlock::kReasonH - FieldH(kReasonSize)) / 2, texts.width);
            FitTo(a_movie, reason, Lang::Get(reasonKey), texts.width);
        }
        SelfTestDrawn(a_movie, a_shown, decision);
    }

    void Click(float a_pressX, float a_pressY, float a_releaseX, float a_releaseY, void*)
    {
        const auto link = SrBlock::ClickedLink(g_linkRects, a_pressX, a_pressY, a_releaseX, a_releaseY);
        if (!link || !Addon::SrPrismaUsable()) return;
        Open(SrBlock::kLinks[*link]);
    }

    void OnMenuOpen()
    {
        g_noResponse = false;
        g_drawnLogs = 0;
    }

    void OnSrPrismaMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg || a_msg->type != SRPrismaAPI::kMessageReply) return;
        if (!a_msg->data || a_msg->dataLen < sizeof(SRPrismaAPI::ReplyMessage)) {
            logger::warn("BlockDraw: an SR Prisma reply of {} bytes (expected {}) - ignored", a_msg->dataLen, sizeof(SRPrismaAPI::ReplyMessage));
            return;
        }
        SRPrismaAPI::ReplyMessage reply{};
        std::memcpy(&reply, a_msg->data, sizeof(reply));  // a copy: the message's data lives only for this call
        if (reply.version != SRPrismaAPI::kReplyVersion) {
            logger::warn("BlockDraw: an SR Prisma reply of version {} (this build reads {}) - ignored", reply.version, SRPrismaAPI::kReplyVersion);
            return;
        }
        if (!g_pending.Matches(reply.requestId, NowMs())) {
            logger::info("BlockDraw: SR Prisma's reply to request {} is not for the pending one ({}) or came too late - ignored", reply.requestId,
                g_pending.Id());
            return;
        }
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::error("BlockDraw: no SKSE task interface - SR Prisma's reply to request {} is dropped", reply.requestId);
            g_pending.Clear(reply.requestId);
            return;
        }
        tasks->AddTask([reply] { HandleReply(reply); });
    }
}
