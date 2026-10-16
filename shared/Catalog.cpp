#include "Catalog.h"

namespace peppy {

namespace {

const char* kColor = "color - a swatch name (\"Paper\", \"Black\", \"Brand Red\"), \"#RRGGBB\", \"none\", {\"cmyk\":[c,m,y,k]} (0-100) or {\"rgb\":[r,g,b]} (0-255)";
const char* kIds = "integer[] - item ids (default: the selection)";
const char* kId = "integer - one item id";
const char* kPage = "string - page number as the Pages panel shows it (\"1\", \"iv\") or its 1-based position (default: the active page)";
const char* kDoc = "integer - document index from document.list (default: the active document)";

Param P(const char* name, const char* doc) { return {name, doc, false}; }
Param R(const char* name, const char* doc) { return {name, doc, true}; }

struct Spec {
	const char* method;
	Kind kind;
	const char* summary;
	std::vector<Param> params;
};

std::vector<Spec> Specs()
{
	auto frameGeometry = [](std::vector<Param> extra) {
		std::vector<Param> p = {R("x", "number - left, in page coordinates"), R("y", "number - top"),
			R("width", "number"), R("height", "number"), P("page", kPage), P("layer", "string - layer name (default: the active layer)"),
			P("name", "string - a name for the new item"), P("fill", kColor), P("stroke", kColor), P("strokeWeight", "number - points"),
			P("objectStyle", "string - object style to apply")};
		p.insert(p.end(), extra.begin(), extra.end());
		return p;
	};

	return {
		// ---- app
		{"app.info", Kind::Read, "InDesign's version, Peppy's version, open documents and whether InDesign can take calls right now.", {}},
		{"commands.list", Kind::Read, "Every command with its parameter docs.", {}},
		{"commands.search", Kind::Read, "Commands whose name, description or parameters mention all of the words.", {R("words", "string")}},

		// ---- documents
		{"document.list", Kind::Read, "Open documents: index, name, path, saved/modified, page count; the active one is marked.", {}},
		{"document.info", Kind::Read, "The active document: name, path, page size, facing pages, margins, bleed, units, pages (with their parent), spreads, layers, counts of stories, styles and swatches.",
			{P("document", kDoc)}},
		{"document.new", Kind::Switch, "New document. Becomes the active one.",
			{P("width", "number - page width in points (default 612)"), P("height", "number - page height (default 792)"), P("pages", "integer - page count (default 1)"),
			 P("facingPages", "boolean (default false)"), P("margins", "number | {top,bottom,inside,outside} - points (default 36)"),
			 P("bleed", "number - points on every side (default 0)"), P("columns", "integer (default 1)"), P("gutter", "number - points"),
			 P("preset", "string - a document preset name, instead of the above")}},
		{"document.open", Kind::Switch, "Open a file by absolute path (.indd, .indt, .idml, .indb not supported) with no dialogs; missing fonts and links are reported, not prompted for.",
			{R("path", "string - absolute path"), P("asCopy", "boolean - open a copy (default false)")}},
		{"document.activate", Kind::Switch, "Bring an open document to the front.", {R("document", kDoc)}},
		{"document.save", Kind::Write, "Save the active document, or save it to a new path (.indd, or .indt for a template). Never shows a dialog.",
			{P("path", "string - Save As: a full path on this Mac, or a file name for the Desktop (default: its own file; a new document needs one)"), P("document", kDoc)}},
		{"document.close", Kind::Switch, "Close a document. Discards unsaved changes unless save is true; never asks.",
			{P("document", kDoc), P("save", "boolean - save first (default false)")}},
		{"document.export", Kind::Write, "Export a copy: PDF (preset, pages), PNG / JPEG (pages, resolution), IDML, EPUB or HTML. Format from 'format' or the path's extension. The document isn't changed. "
			"Without 'path', the first page asked for (or the active page) comes back as an image (JPEG, at most 1024 px) for you to look at.",
			{P("path", "string - a full path on this Mac, or a file name for the Desktop; leave out to get a picture back"), P("page", kPage), P("format", "string - pdf, png, jpg, idml, epub, html (default: the extension)"),
			 P("pages", "string - page range as in InDesign's dialog (\"1-3, 5\", \"all\"; default all)"), P("pdfPreset", "string - e.g. \"[High Quality Print]\", \"[Smallest File Size]\", \"[PDF/X-4:2008]\""),
			 P("resolution", "number - png/jpg dpi (default 144)"), P("spreads", "boolean - export spreads, not pages (default false)"),
			 P("bleed", "boolean - include the document bleed (default false)"), P("transparent", "boolean - png background (default false)")}},
		{"document.preflight", Kind::Read, "Run the live preflight: errors with the page and item they're on (overset text, missing links and fonts, low resolution...).",
			{P("profile", "string - preflight profile name (default: the document's)")}},

		// ---- pages, spreads, parents
		{"page.list", Kind::Read, "Pages: name, 1-based position, spread, side, applied parent, bounds, item count.", {P("document", kDoc)}},
		{"page.add", Kind::Edit, "Add pages after a page (or at the end).",
			{P("after", kPage), P("count", "integer (default 1)"), P("parent", "string - parent spread to apply, e.g. \"A-Parent\" or \"None\"")}},
		{"page.delete", Kind::Edit, "Delete pages and everything on them.", {R("pages", "string - page range, e.g. \"3\", \"4-6\"")}},
		{"page.move", Kind::Edit, "Move a page after another page (or to the start with after = \"0\").", {R("page", kPage), R("after", kPage)}},
		{"page.set", Kind::Edit, "Change a page: its parent, size (just this page), margins, columns.",
			{R("page", kPage), P("parent", "string - parent spread name or \"None\""), P("width", "number"), P("height", "number"),
			 P("margins", "number | {top,bottom,left,right}"), P("columns", "integer"), P("gutter", "number")}},
		{"page.go", Kind::Read, "Show a page in the window (scrolls the view, changes nothing).", {R("page", kPage)}},
		{"parent.list", Kind::Read, "Parent spreads (master pages): name, prefix, based-on, pages that use them, items.", {}},
		{"parent.create", Kind::Edit, "New parent spread, optionally based on another.",
			{P("prefix", "string - e.g. \"B\""), P("name", "string - e.g. \"Chapter\""), P("basedOn", "string - parent name")}},
		{"parent.override", Kind::Edit, "Override parent items on a page so they can be edited there (all of them, or ids from parent.list).",
			{R("page", kPage), P("ids", "integer[] - parent item ids (default: all)")}},

		// ---- layers
		{"layer.list", Kind::Read, "Layers, top to bottom: id, name, visible, locked, printable, color, item count; the active one is marked.", {}},
		{"layer.create", Kind::Edit, "New layer (on top, or above / below another).",
			{R("name", "string"), P("above", "string - layer name"), P("below", "string - layer name"), P("color", "string - UI color name, e.g. \"Red\"")}},
		{"layer.set", Kind::Edit, "Change a layer: rename, show / hide, lock, printable, make active, move, delete.",
			{R("layer", "string - layer name"), P("name", "string - new name"), P("visible", "boolean"), P("locked", "boolean"), P("printable", "boolean"),
			 P("active", "boolean"), P("above", "string - move above this layer"), P("delete", "boolean - delete it and its items")}},

		// ---- page items
		{"item.tree", Kind::Read, "Items on a page or spread, front to back, nested in their groups: id, type, name, bounds, layer, and for frames their content (text excerpt, image file).",
			{P("page", kPage), P("spread", "integer - 1-based spread position, instead of page"), P("parent", "string - list a parent spread's items"),
			 P("layer", "string - only this layer"), P("depth", "integer - group levels to open (default 3)")}},
		{"item.get", Kind::Read, "One item in detail: type, bounds, rotation, fill, stroke, corner, opacity, effects, text frame options, threading, image info, applied object style.",
			{R("id", kId)}},
		{"item.selection", Kind::Read, "The selected items (or the text selection: story, range, contents).", {}},
		{"item.select", Kind::Read, "Select items by id (replaces the selection unless add is true; no ids clears it).",
			{P("ids", "integer[]"), P("add", "boolean")}},
		{"item.set", Kind::Edit, "Change items: name, fill, stroke, stroke weight / type / alignment, opacity, corner radius / shape, locked, visible, object style, text wrap.",
			{P("ids", kIds), P("name", "string"), P("fill", kColor), P("fillTint", "number 0-100"), P("stroke", kColor), P("strokeTint", "number 0-100"),
			 P("strokeWeight", "number"), P("strokeType", "string - Solid, Dashed, Dotted, Thick - Thin..."), P("strokeAlignment", "string - center, inside, outside"),
			 P("opacity", "number 0-100"), P("blendMode", "string - normal, multiply, screen, overlay..."), P("cornerRadius", "number"),
			 P("cornerShape", "string - rounded, bevel, inset, inverseRounded, fancy, none"), P("locked", "boolean"), P("visible", "boolean"),
			 P("objectStyle", "string - apply an object style"), P("clearOverrides", "boolean - with objectStyle"),
			 P("textWrap", "string - none, boundingBox, contour, jumpObject, nextColumn"), P("wrapOffset", "number")}},
		{"item.bounds", Kind::Edit, "Set an item's exact position and / or size in page coordinates (any of x, y, width, height).",
			{R("id", kId), P("x", "number"), P("y", "number"), P("width", "number"), P("height", "number"),
			 P("anchor", "string - topLeft (default), center, ... - the point that stays put when resizing"), P("scaleContent", "boolean - scale the frame's content too (default false)")}},
		{"item.transform", Kind::Edit, "Move, rotate, scale or shear items about their center or an origin.",
			{P("ids", kIds), P("translate", "number[] - [dx, dy] points"), P("rotate", "number - degrees, counterclockwise"),
			 P("scale", "number | number[] - factor (1 = unchanged) or [sx, sy]"), P("shear", "number - degrees"), P("origin", "number[] - [x, y] page coordinates (default: the items' center)")}},
		{"item.arrange", Kind::Edit, "Bring to front, send to back, forward or backward; or move to a layer.",
			{P("ids", kIds), P("order", "string - front, back, forward, backward"), P("layer", "string - move to this layer")}},
		{"item.align", Kind::Edit, "Align or distribute items relative to each other, the margins, the page or the spread.",
			{P("ids", kIds), P("align", "string - left, centerX, right, top, centerY, bottom"), P("distribute", "string - horizontal, vertical, spaceHorizontal, spaceVertical"),
			 P("to", "string - selection (default), margins, page, spread, keyObject"), P("key", "integer - key object id with to: keyObject")}},
		{"item.duplicate", Kind::Edit, "Duplicate items, offset or onto another page.", {P("ids", kIds), P("translate", "number[] - [dx, dy] (default [0,0])"), P("page", "string - duplicate onto this page")}},
		{"item.group", Kind::Edit, "Group items. Returns the group.", {P("ids", kIds), P("name", "string")}},
		{"item.ungroup", Kind::Edit, "Ungroup groups. Returns their items.", {P("ids", kIds)}},
		{"item.delete", Kind::Edit, "Delete items.", {P("ids", kIds)}},
		{"item.overlaps", Kind::Read, "What overlaps or touches what: for one item, every visible item on its spread whose frame overlaps it (or comes within 'margin'), with which is on top; for a page, every such pair. Catches text running into a photo, a caption over a button, a frame past the margin.",
			{P("id", "integer - check this item (or give page)"), P("page", kPage), P("margin", "number - also report items this close, in points (default 0)"),
			 P("textOnly", "boolean - page mode: only pairs that include a text frame (default false)"), P("limit", "integer - most pairs to return (default 200)")}},
		{"item.fit", Kind::Edit, "Fit a frame and its content: content to frame, frame to content, proportionally, fill proportionally, center.",
			{P("ids", kIds), R("fit", "string - contentToFrame, frameToContent, proportionally, fillProportionally, center, contentAware")}},
		{"item.anchor", Kind::Edit, "Anchor an item into text (inline or custom position) at a story position.",
			{R("id", kId), R("story", "integer - story id"), R("index", "integer - character index"), P("position", "string - inline (default), aboveLine, anchored")}},

		// ---- creating
		{"frame.text", Kind::Edit, "New text frame, optionally with text and a paragraph style.",
			frameGeometry({P("contents", "string - the text (\\r or \\n = new paragraph)"), P("paragraphStyle", "string"), P("columns", "integer"),
			               P("inset", "number - inset spacing on every side"), P("verticalJustification", "string - top, center, bottom, justify"),
			               P("autoSize", "string - off, heightOnly, widthOnly, heightAndWidth")})},
		{"frame.rect", Kind::Edit, "New rectangle (an empty graphics frame unless filled).", frameGeometry({P("cornerRadius", "number")})},
		{"frame.ellipse", Kind::Edit, "New ellipse.", frameGeometry({})},
		{"frame.polygon", Kind::Edit, "New polygon or star.", frameGeometry({P("sides", "integer (default 6)"), P("inset", "number 0-100 - star inset percent (default 0)")})},
		{"frame.line", Kind::Edit, "New straight line from (x1, y1) to (x2, y2).",
			{R("x1", "number"), R("y1", "number"), R("x2", "number"), R("y2", "number"), P("page", kPage), P("layer", "string"),
			 P("stroke", kColor), P("strokeWeight", "number (default 1)"), P("name", "string")}},
		{"frame.path", Kind::Edit, "New path from points: [x,y] corners or {p:[x,y], in:[x,y], out:[x,y]} Bézier anchors.",
			{R("points", "array - anchors"), P("closed", "boolean"), P("page", kPage), P("layer", "string"), P("fill", kColor), P("stroke", kColor),
			 P("strokeWeight", "number"), P("name", "string")}},

		// ---- text
		{"text.get", Kind::Read, "A story's text and how it's styled: paragraphs with their paragraph style, character style runs, overset, threaded frames.",
			{P("id", "integer - a text frame id or story id (default: the selection)"), P("styled", "boolean - include style runs (default true)"),
			 P("from", "integer - first character"), P("count", "integer - characters (default all, up to 20000)")}},
		{"text.set", Kind::Edit, "Replace a story's text, or a range of it, or insert at a position. Keeps the frame's threading.",
			{P("id", "integer - text frame or story id (default: the selection)"), R("contents", "string - \\r or \\n = new paragraph"),
			 P("from", "integer - first character to replace"), P("count", "integer - characters to replace (0 = insert)"),
			 P("at", "string - start, end (insert there instead of replacing)")}},
		{"text.format", Kind::Edit, "Format text: font, style, size, leading, tracking, kerning, color, alignment, indents, space before / after, case, paragraph or character style - on a whole story, a range or paragraphs.",
			{P("id", "integer - frame or story id (default: the selection)"), P("from", "integer"), P("count", "integer"),
			 P("paragraphs", "number[] - 0-based paragraph indexes instead of from/count"),
			 P("font", "string - family, e.g. \"Minion Pro\""), P("fontStyle", "string - e.g. \"Bold Italic\""), P("size", "number - points"),
			 P("leading", "number | \"auto\""), P("tracking", "number"), P("kerning", "string - metrics, optical, or a number"), P("color", kColor), P("tint", "number"),
			 P("align", "string - left, center, right, justify, justifyAll, towardsSpine, awayFromSpine"), P("leftIndent", "number"), P("rightIndent", "number"),
			 P("firstLineIndent", "number"), P("spaceBefore", "number"), P("spaceAfter", "number"), P("case", "string - normal, allCaps, smallCaps"),
			 P("underline", "boolean"), P("strikethrough", "boolean"), P("baselineShift", "number"), P("hyphenation", "boolean"),
			 P("paragraphStyle", "string - apply a paragraph style"), P("characterStyle", "string - apply a character style"), P("clearOverrides", "boolean")}},
		{"text.find", Kind::Read, "Find text (plain, wildcard or GREP) across the document or a story: each hit's story, frame, page, position and text.",
			{R("find", "string"), P("grep", "boolean - find is a GREP expression (default false)"), P("caseSensitive", "boolean"), P("wholeWord", "boolean"),
			 P("id", "integer - only this story / frame"), P("paragraphStyle", "string - only text in this style"), P("limit", "integer (default 200)")}},
		{"text.replace", Kind::Edit, "Find and replace text (plain or GREP, with $1 groups), optionally also changing formatting. Returns how many changed.",
			{R("find", "string"), R("replace", "string"), P("grep", "boolean"), P("caseSensitive", "boolean"), P("wholeWord", "boolean"),
			 P("id", "integer - only this story / frame"), P("paragraphStyle", "string - apply to what changed"), P("characterStyle", "string")}},
		{"text.thread", Kind::Edit, "Thread text frames in order (the story flows from the first into the next), or unthread one.",
			{P("ids", "integer[] - frames in reading order"), P("unthread", "integer - break the thread after this frame")}},
		{"text.place", Kind::Edit, "Place a text file (.docx, .rtf, .txt, .md as text) into a frame or a new frame, autoflowing onto new pages if asked.",
			{R("path", "string - absolute"), P("id", "integer - target frame"), P("autoflow", "boolean - add pages until it all fits"),
			 P("keepFormatting", "boolean (default true)"), P("x", "number"), P("y", "number"), P("width", "number"), P("height", "number"), P("page", kPage)}},
		{"text.overset", Kind::Read, "Stories with overset text: frame, page, how many characters don't fit.", {}},
		{"text.outline", Kind::Edit, "Convert a text frame's text to outlines. Returns the new item.", {R("id", kId)}},
		{"font.list", Kind::Read, "Fonts: family and styles, installed or used in the document; missing ones flagged.",
			{P("search", "string - filter by name"), P("used", "boolean - only fonts the document uses")}},

		// ---- styles and swatches
		{"style.list", Kind::Read, "Paragraph, character, object, table or cell styles (with groups), and how often each is used.",
			{R("kind", "string - paragraph, character, object, table, cell"), P("details", "boolean - include each style's settings")}},
		{"style.create", Kind::Edit, "New style (or update an existing one) from settings, based on another, or from an item / text.",
			{R("kind", "string - paragraph, character, object"), R("name", "string"), P("basedOn", "string"), P("nextStyle", "string - paragraph styles"),
			 P("group", "string - style group path, e.g. \"Body/Lists\""), P("from", "integer - item or frame id to take the formatting from"),
			 P("settings", "object - the same keys text.format / item.set take (font, size, leading, color, fill, stroke...)")}},
		{"style.apply", Kind::Edit, "Apply a style to items (object style) or text (paragraph / character style).",
			{R("kind", "string - paragraph, character, object"), R("name", "string"), P("ids", kIds), P("from", "integer"), P("count", "integer"),
			 P("paragraphs", "number[]"), P("clearOverrides", "boolean")}},
		{"style.delete", Kind::Edit, "Delete a style, replacing it where it's used.",
			{R("kind", "string"), R("name", "string"), P("replaceWith", "string - style name (default: [No Paragraph Style] / [None])")}},
		{"swatch.list", Kind::Read, "Swatches: name, model (process / spot), space, values, tints, gradients; which are used.", {}},
		{"swatch.create", Kind::Edit, "New color swatch, tint or gradient.",
			{R("name", "string"), P("cmyk", "number[] - [c,m,y,k] 0-100"), P("rgb", "number[] - [r,g,b] 0-255"), P("hex", "string - #RRGGBB"),
			 P("spot", "boolean - a spot color (default process)"), P("tintOf", "string - make a tint of this swatch"), P("tint", "number 0-100"),
			 P("gradient", "array - stops [{color, location 0-100}] for a gradient swatch"), P("radial", "boolean")}},
		{"swatch.delete", Kind::Edit, "Delete a swatch, replacing it where it's used.", {R("name", "string"), P("replaceWith", "string (default \"Black\" / \"None\")")}},

		// ---- images and links
		{"image.place", Kind::Edit, "Place an image, PDF, .ai or .indd file: into a frame, or as a new frame at x, y (and width / height to fit it).",
			{R("path", "string - absolute"), P("id", "integer - frame to place into"), P("x", "number"), P("y", "number"), P("width", "number"), P("height", "number"),
			 P("page", kPage), P("layer", "string"), P("fit", "string - proportionally (default for new frames), fillProportionally, contentToFrame, frameToContent, none"),
			 P("pdfPage", "integer - which page of a PDF / .indd"), P("link", "boolean - link (default) or embed")}},
		{"link.list", Kind::Read, "Placed files: id, file, status (normal, missing, out of date, embedded), page, frame, effective resolution, scale.", {P("problems", "boolean - only missing / out-of-date / low-res")}},
		{"link.update", Kind::Edit, "Update out-of-date links, or relink one to another file.",
			{P("ids", "integer[] - link ids (default: every out-of-date link)"), P("relink", "string - absolute path for the one link given")}},

		// ---- tables
		{"table.create", Kind::Edit, "New table: in a new text frame, or at a story position. Optional contents fill it row by row.",
			{R("rows", "integer"), R("columns", "integer"), P("contents", "array - rows of cell strings"), P("headerRows", "integer"),
			 P("id", "integer - text frame or story to insert into"), P("at", "integer - character index (default: end)"),
			 P("x", "number"), P("y", "number"), P("width", "number"), P("height", "number"), P("page", kPage), P("tableStyle", "string")}},
		{"table.get", Kind::Read, "A table's rows, columns, cell contents, header / footer rows, styles.", {R("id", "integer - table id (from text.get or item.get)")}},
		{"table.set", Kind::Edit, "Fill cells, add / delete rows and columns, merge, set widths and heights, cell fill and styles.",
			{R("id", "integer - table id"), P("cells", "array - [{row, column, contents?, fill?, cellStyle?, paragraphStyle?}]"),
			 P("contents", "array - rows of cell strings from row 0"), P("addRows", "integer"), P("addColumns", "integer"), P("deleteRows", "number[]"),
			 P("deleteColumns", "number[]"), P("columnWidths", "number[]"), P("rowHeights", "number[]"), P("merge", "object - {row, column, rows, columns}"),
			 P("headerRows", "integer"), P("footerRows", "integer"), P("tableStyle", "string")}},

		// ---- view
		{"view.screenshot", Kind::Read, "Render a page or spread and return it as an image to look at (JPEG, at most 1024 px; the document isn't touched).",
			{P("page", kPage), P("spread", "boolean - the page's whole spread"), P("resolution", "number - dpi (default 72)"),
			 P("path", "string - also save the full-size PNG here (a full path, or a file name for the Desktop)")}},
		{"view.set", Kind::Read, "Zoom and scroll the window: fit page / spread, zoom percent, show an item. Changes nothing in the document.",
			{P("zoom", "number - percent"), P("fit", "string - page, spread, pasteboard"), P("page", kPage), P("id", "integer - show this item"),
			 P("mode", "string - normal, preview, bleed, slug, presentation")}},

		// ---- history and escape hatches
		{"history.undo", Kind::Undo, "Undo steps in a document (Edit > Undo); a peppy_batch is one step.", {P("steps", "integer (default 1)"), P("document", kDoc)}},
		{"history.redo", Kind::Undo, "Redo steps.", {P("steps", "integer (default 1)"), P("document", kDoc)}},
		{"action.list", Kind::Read, "InDesign's menu actions (every menu command and keyboard shortcut target): id, name, area, enabled.", {P("search", "string - filter by name")}},
		{"action.run", Kind::Edit, "Run a menu action by id or exact name (from action.list), as if chosen from the menu. Refused if it would open a dialog.",
			{P("id", "integer"), P("name", "string")}},
		{"script.run", Kind::Edit, "Last resort: run InDesign JavaScript (the scripting DOM, 'app' and all) as one undo step and return its last value as JSON. Prefer a command; tell the person why you needed this.",
			{R("code", "string - JavaScript"), P("language", "string - javascript (ExtendScript, default) or uxp")}},
	};
}

} // namespace

void RegisterCatalog(const std::map<std::string, Runner>& runners, std::function<Runner(const std::string& method)> fallback)
{
	for (Spec& s : Specs()) {
		Command c;
		c.method = s.method;
		c.summary = s.summary;
		c.params = s.params;
		c.kind = s.kind;
		auto it = runners.find(s.method);
		if (it != runners.end()) c.run = it->second;
		else if (fallback) c.run = fallback(s.method);
		else {
			std::string m = s.method;
			c.run = [m](const Json&) -> Json {
				throw Error(kErrMethodNotFound, m + " isn't built yet in this version of Peppy", "use another command, or script.run as a last resort");
			};
		}
		Register(std::move(c));
	}
	// Runners for commands outside the catalog (host-specific) register too.
	for (auto& [method, run] : runners)
		if (!Find(method)) Register({method, method, {}, Kind::Read, run});

	if (auto it = runners.find("commands.list"); it == runners.end())
		Register({"commands.list", "Every command with its parameter docs.", {}, Kind::Read, [](const Json&) {
			Json out = Json::array();
			for (const Command& c : All()) out.push_back(Describe(c));
			return out;
		}});
	if (auto it = runners.find("commands.search"); it == runners.end())
		Register({"commands.search", "Commands whose name, description or parameters mention all of the words.", {R("words", "string")}, Kind::Read,
			[](const Json& p) {
				Json out = Json::array();
				for (const Command* c : Search(Str(p, "words"))) out.push_back(Describe(*c));
				return out;
			}});
}

const std::vector<std::string>& Everyday()
{
	static const std::vector<std::string> everyday = {
		"document.info", "page.list", "item.tree", "item.get", "item.selection", "item.set", "item.bounds", "item.transform", "item.delete",
		"frame.text", "frame.rect", "text.get", "text.set", "text.format", "style.apply", "image.place", "view.screenshot", "document.export", "history.undo"};
	return everyday;
}

std::string Instructions()
{
	return "Peppy drives Adobe InDesign directly through a native plug-in (no ExtendScript for normal work). The everyday commands are tools here; "
	       "find the rest (styles, swatches, tables, parents, layers, links, find/replace, preflight...) with peppy_find and run them with peppy_call. "
	       "Units are points. Coordinates are page coordinates: origin at the page's top-left, y growing down; bounds are {x, y, width, height}. "
	       "'page' is the page name the Pages panel shows (\"1\", \"iv\") or a 1-based position. Item ids are InDesign's integer ids, stable across saves; "
	       "commands that take ids act on the selection when none are given. Start with document_info and item_tree. "
	       "Every call is one step on Edit > Undo; peppy_batch makes several calls one step, and a later step can use \"$0.id\" from an earlier one. "
	       "Nothing is saved by surprise and no dialog ever opens: document.close discards unless save is true. "
	       "Use view_screenshot to look at your work.";
}

} // namespace peppy
