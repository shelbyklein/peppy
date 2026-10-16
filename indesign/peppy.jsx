// peppy.jsx - Peppy's command library, in InDesign's scripting DOM (ExtendScript, ES3).
// Loaded once into the persistent "peppy" engine by the plug-in; each call is
// __peppy.run(method, paramsJson) -> JSON {result} or {error: {code, message, hint}}.
// It runs inside the plug-in's undo step, on the main thread, with dialogs off.
// ExtendScript quirk: always parenthesize a chained ?: (a ? b : (c ? d : e)), or it parses wrong.
//
// Units are points; coordinates are page coordinates (origin at the page's
// top-left, y down); bounds are {x, y, width, height}.

var __peppy = (function () {

	// ---------------------------------------------------------------- JSON

	function quote(s) {
		return '"' + String(s).replace(/[\\"\u0000-\u001f\u2028\u2029]/g, function (c) {
			var m = {'"': '\\"', '\\': '\\\\', '\n': '\\n', '\r': '\\r', '\t': '\\t', '\b': '\\b', '\f': '\\f'};
			return m[c] || '\\u' + ('0000' + c.charCodeAt(0).toString(16)).slice(-4);
		}) + '"';
	}
	function stringify(v) {
		if (v === null || v === undefined) return 'null';
		var t = typeof v;
		if (t === 'number') return isFinite(v) ? String(Math.round(v * 10000) / 10000) : 'null';
		if (t === 'boolean') return v ? 'true' : 'false';
		if (t === 'string') return quote(v);
		if (v instanceof Array) {
			var a = [];
			for (var i = 0; i < v.length; i++) a.push(stringify(v[i]));
			return '[' + a.join(',') + ']';
		}
		if (t === 'object') {
			var o = [];
			for (var k in v) if (v.hasOwnProperty(k) && v[k] !== undefined && typeof v[k] !== 'function') o.push(quote(k) + ':' + stringify(v[k]));
			return '{' + o.join(',') + '}';
		}
		return quote(String(v));
	}
	function parse(s) { return eval('(' + s + ')'); }   // only ever the plug-in's own JSON

	// ---------------------------------------------------------------- errors

	var kInvalidParams = -32602, kHost = -32000, kUnavailable = -32002, kNotFound = -32004;
	function fail(code, message, hint) { throw {peppy: true, code: code, message: message, hint: hint || ''}; }
	function need(p, key) { if (p[key] === undefined || p[key] === null) fail(kInvalidParams, "missing '" + key + "'"); return p[key]; }
	function has(p, key) { return p[key] !== undefined && p[key] !== null; }

	// ---------------------------------------------------------------- documents, pages, coordinates

	function doc(p) {
		if (app.documents.length === 0) fail(kUnavailable, 'no document is open', 'open one with document.open or make one with document.new');
		if (p && has(p, 'document')) {
			var d = app.documents[p.document];
			if (!d || !d.isValid) fail(kNotFound, 'no document ' + p.document, 'document.list shows the open documents');
			return d;
		}
		return app.activeDocument;
	}

	function activePage(d) {
		try { if (app.activeWindow && app.activeWindow.activePage && app.activeWindow.activePage.parent.parent == d) return app.activeWindow.activePage; } catch (e) {}
		return d.pages[0];
	}

	// page: a name as the Pages panel shows it, or a 1-based position.
	function pageOf(d, value) {
		if (value === undefined || value === null || value === '') return activePage(d);
		var s = String(value);
		var byName = d.pages.itemByName(s);
		if (byName.isValid) return byName;
		var n = parseInt(s, 10);
		if (!isNaN(n) && String(n) === s && n >= 1 && n <= d.pages.length) return d.pages[n - 1];
		for (var i = 0; i < d.masterSpreads.length; i++) {
			var ms = d.masterSpreads[i];
			if (ms.name === s) return ms.pages[0];
			for (var j = 0; j < ms.pages.length; j++) if (ms.name + ':' + (j + 1) === s) return ms.pages[j];
		}
		fail(kNotFound, "no page '" + s + "'", 'page.list shows the pages');
	}

	function pageOrigin(page) { var b = page.bounds; return [b[1], b[0]]; }   // [x, y] in ruler coordinates

	// geometricBounds [y1, x1, y2, x2] -> {x, y, width, height} on the item's page
	function boundsOf(item, page) {
		var g = item.geometricBounds;
		page = page || pageFor(item);
		var o = page ? pageOrigin(page) : [0, 0];
		return {x: g[1] - o[0], y: g[0] - o[1], width: g[3] - g[1], height: g[2] - g[0]};
	}
	function toGeometric(b, page) {
		var o = pageOrigin(page);
		return [b.y + o[1], b.x + o[0], b.y + o[1] + b.height, b.x + o[0] + b.width];
	}
	function pageFor(item) {
		try { if (item.parentPage) return item.parentPage; } catch (e) {}
		return null;
	}
	function pageName(page) {
		if (!page) return null;
		try { if (page.parent instanceof MasterSpread) return page.parent.name + ':' + (page.index + 1); } catch (e) {}
		return page.name;
	}

	// ---------------------------------------------------------------- items

	function findItem(d, id) {
		id = Number(id);
		if (isNaN(id)) fail(kInvalidParams, 'ids are integers');
		var it = d.pageItems.itemByID(id);
		if (it.isValid) return it.getElements()[0];
		var all = d.allPageItems;
		for (var i = 0; i < all.length; i++) if (all[i].id === id) return all[i];
		var st = d.stories.itemByID(id);
		if (st.isValid) return st.getElements()[0];
		fail(kNotFound, 'no item ' + id, 'item.tree lists the items on a page');
	}

	function selectionItems(d) {
		var out = [];
		var sel = app.selection;
		for (var i = 0; i < sel.length; i++) {
			var s = sel[i];
			if (s.hasOwnProperty('geometricBounds')) out.push(s);
			else if (s.hasOwnProperty('parentTextFrames') && s.parentTextFrames.length) out.push(s.parentTextFrames[0]);
		}
		return out;
	}

	function targets(d, p) {
		var ids = has(p, 'ids') ? p.ids : (has(p, 'id') ? [p.id] : null);   // ExtendScript misparses unparenthesized chained ?:
		if (ids === null) {
			var sel = selectionItems(d);
			if (!sel.length) fail(kInvalidParams, 'no ids given and nothing is selected', 'pass ids, or select items with item.select');
			return sel;
		}
		if (!(ids instanceof Array)) ids = [ids];
		var out = [];
		for (var i = 0; i < ids.length; i++) out.push(findItem(d, ids[i]));
		return out;
	}

	function typeOf(item) { return item.constructor.name; }

	function brief(item) {
		var b = {id: item.id, type: typeOf(item), name: item.name || ''};
		try { b.bounds = boundsOf(item); } catch (e) {}
		try { b.page = pageName(pageFor(item)); } catch (e) {}
		try { if (item instanceof TextFrame) b.text = item.parentStory.contents.substr(0, 80); } catch (e) {}
		return b;
	}

	function describe(item, depth) {
		var out = brief(item);
		try { out.layer = item.itemLayer.name; } catch (e) {}
		try { out.locked = item.locked; out.visible = item.visible; } catch (e) {}
		try { if (item instanceof TextFrame) { out.story = item.parentStory.id; out.overflows = item.overflows; } } catch (e) {}
		try {
			if (item.graphics && item.graphics.length) {
				var g = item.graphics[0];
				out.image = {type: typeOf(g)};
				try { out.image.file = g.itemLink.filePath; out.image.status = String(g.itemLink.status); } catch (e2) {}
			}
		} catch (e) {}
		if (depth > 0 && item.pageItems && (item instanceof Group)) {
			out.children = [];
			for (var i = 0; i < item.pageItems.length; i++) out.children.push(describe(item.pageItems[i].getElements()[0], depth - 1));
		}
		return out;
	}

	function detail(item) {
		var out = describe(item, 1);
		var props = ['rotationAngle', 'shearAngle', 'strokeWeight', 'cornerRadius', 'fillTint', 'strokeTint'];
		for (var i = 0; i < props.length; i++) try { out[props[i]] = item[props[i]]; } catch (e) {}
		try { out.fill = colorName(item.fillColor); out.stroke = colorName(item.strokeColor); } catch (e) {}
		try { out.opacity = item.transparencySettings.blendingSettings.opacity; } catch (e) {}
		try { out.objectStyle = item.appliedObjectStyle.name; } catch (e) {}
		try { out.textWrap = String(item.textWrapPreferences.textWrapMode); } catch (e) {}
		try {
			if (item instanceof TextFrame) {
				out.columns = item.textFramePreferences.textColumnCount;
				out.inset = item.textFramePreferences.insetSpacing;
				out.previous = item.previousTextFrame ? item.previousTextFrame.id : null;
				out.next = item.nextTextFrame ? item.nextTextFrame.id : null;
			}
		} catch (e) {}
		return out;
	}

	// ---------------------------------------------------------------- colors

	function colorName(c) { try { return c.name; } catch (e) { return null; } }

	function colorFor(d, spec) {
		if (spec === undefined || spec === null) return undefined;
		if (typeof spec === 'string') {
			var low = spec.toLowerCase();
			if (low === 'none') return d.swatches.itemByName('None');
			if (/^#?[0-9a-f]{6}$/i.test(spec)) {
				var hex = spec.replace('#', '');
				return rgbSwatch(d, [parseInt(hex.substr(0, 2), 16), parseInt(hex.substr(2, 2), 16), parseInt(hex.substr(4, 2), 16)]);
			}
			var sw = d.swatches.itemByName(spec);
			if (sw.isValid) return sw;
			fail(kNotFound, "no swatch '" + spec + "'", 'swatch.list shows the swatches; or pass "#RRGGBB"');
		}
		if (spec.cmyk) return processColor(d, ColorSpace.CMYK, spec.cmyk);
		if (spec.rgb) return rgbSwatch(d, spec.rgb);
		if (spec.swatch) return colorFor(d, spec.swatch);
		fail(kInvalidParams, 'colors are a swatch name, "#RRGGBB", "none", {cmyk:[...]} or {rgb:[...]}');
	}
	function rgbSwatch(d, rgb) { return processColor(d, ColorSpace.RGB, rgb); }
	function processColor(d, space, values) {
		var name = (space == ColorSpace.RGB ? 'R=' + values[0] + ' G=' + values[1] + ' B=' + values[2]
			: 'C=' + values[0] + ' M=' + values[1] + ' Y=' + values[2] + ' K=' + values[3]);
		var c = d.colors.itemByName(name);
		if (c.isValid) return c;
		return d.colors.add({name: name, model: ColorModel.PROCESS, space: space, colorValue: values});
	}

	// ---------------------------------------------------------------- shared setters

	function layerFor(d, name) {
		if (!name) return d.activeLayer;
		var l = d.layers.itemByName(name);
		if (!l.isValid) fail(kNotFound, "no layer '" + name + "'", 'layer.list shows the layers');
		return l;
	}

	function applyLook(d, item, p) {
		if (has(p, 'name')) item.name = String(p.name);
		if (has(p, 'fill')) item.fillColor = colorFor(d, p.fill);
		if (has(p, 'fillTint')) item.fillTint = p.fillTint;
		if (has(p, 'stroke')) item.strokeColor = colorFor(d, p.stroke);
		if (has(p, 'strokeTint')) item.strokeTint = p.strokeTint;
		if (has(p, 'strokeWeight')) item.strokeWeight = p.strokeWeight;
		if (has(p, 'strokeType')) {
			var st = d.strokeStyles.itemByName(p.strokeType);
			if (!st.isValid) fail(kNotFound, "no stroke type '" + p.strokeType + "'");
			item.strokeType = st;
		}
		if (has(p, 'strokeAlignment')) item.strokeAlignment = {center: StrokeAlignment.CENTER_ALIGNMENT, inside: StrokeAlignment.INSIDE_ALIGNMENT, outside: StrokeAlignment.OUTSIDE_ALIGNMENT}[p.strokeAlignment];
		if (has(p, 'opacity')) item.transparencySettings.blendingSettings.opacity = p.opacity;
		if (has(p, 'blendMode')) item.transparencySettings.blendingSettings.blendMode = BlendMode[String(p.blendMode).toUpperCase()];
		if (has(p, 'cornerRadius')) {
			item.topLeftCornerRadius = item.topRightCornerRadius = item.bottomLeftCornerRadius = item.bottomRightCornerRadius = p.cornerRadius;
			if (!has(p, 'cornerShape')) item.topLeftCornerOption = item.topRightCornerOption = item.bottomLeftCornerOption = item.bottomRightCornerOption = CornerOptions.ROUNDED_CORNER;
		}
		if (has(p, 'cornerShape')) {
			var shapes = {rounded: CornerOptions.ROUNDED_CORNER, bevel: CornerOptions.BEVEL_CORNER, inset: CornerOptions.INSET_CORNER,
				inverseRounded: CornerOptions.INVERSE_ROUNDED_CORNER, fancy: CornerOptions.FANCY_CORNER, none: CornerOptions.NONE};
			item.topLeftCornerOption = item.topRightCornerOption = item.bottomLeftCornerOption = item.bottomRightCornerOption = shapes[p.cornerShape];
		}
		if (has(p, 'objectStyle')) {
			var os = d.objectStyles.itemByName(p.objectStyle);
			if (!os.isValid) fail(kNotFound, "no object style '" + p.objectStyle + "'", "style.list with kind 'object'");
			item.applyObjectStyle(os, !!p.clearOverrides);
		}
		if (has(p, 'textWrap')) {
			var modes = {none: TextWrapModes.NONE, boundingBox: TextWrapModes.BOUNDING_BOX_TEXT_WRAP, contour: TextWrapModes.CONTOUR,
				jumpObject: TextWrapModes.JUMP_OBJECT_TEXT_WRAP, nextColumn: TextWrapModes.NEXT_COLUMN_TEXT_WRAP};
			item.textWrapPreferences.textWrapMode = modes[p.textWrap];
		}
		if (has(p, 'wrapOffset')) item.textWrapPreferences.textWrapOffset = [p.wrapOffset, p.wrapOffset, p.wrapOffset, p.wrapOffset];
		if (has(p, 'locked')) item.locked = !!p.locked;
		if (has(p, 'visible')) item.visible = !!p.visible;
	}

	function newFrame(p, collection) {
		var d = doc(p);
		need(p, 'x'); need(p, 'y'); need(p, 'width'); need(p, 'height');
		var page = pageOf(d, p.page);
		var item = page[collection].add(layerFor(d, p.layer), undefined, undefined, {geometricBounds: toGeometric(p, page)});
		applyLook(d, item, p);
		return {d: d, page: page, item: item};
	}

	// ---------------------------------------------------------------- text

	function storyOf(d, p) {
		var target;
		if (has(p, 'id')) target = findItem(d, p.id);
		else {
			var sel = app.selection;
			if (!sel.length) fail(kInvalidParams, 'no id given and nothing is selected');
			target = sel[0];
		}
		if (target instanceof Story) return target;
		if (target.hasOwnProperty('parentStory')) return target.parentStory;
		fail(kInvalidParams, 'item ' + target.id + " isn't a text frame", 'pass a text frame id or story id');
	}

	function textRange(story, p) {
		if (has(p, 'paragraphs')) {
			var out = [];
			for (var i = 0; i < p.paragraphs.length; i++) {
				var para = story.paragraphs[p.paragraphs[i]];
				if (!para.isValid) fail(kNotFound, 'no paragraph ' + p.paragraphs[i]);
				out.push(para);
			}
			return out;
		}
		if (has(p, 'from')) {
			var n = has(p, 'count') ? p.count : story.characters.length - p.from;
			if (n <= 0) fail(kInvalidParams, 'count must be at least 1');
			return [story.characters.itemByRange(p.from, p.from + n - 1)];
		}
		return [story.texts[0]];
	}

	function styleNamed(d, kind, name) {
		var col = {paragraph: d.allParagraphStyles, character: d.allCharacterStyles, object: d.allObjectStyles, table: d.allTableStyles, cell: d.allCellStyles}[kind];
		if (!col) fail(kInvalidParams, "kind is paragraph, character, object, table or cell");
		for (var i = 0; i < col.length; i++) if (col[i].name === name || stylePath(col[i]) === name) return col[i];
		fail(kNotFound, 'no ' + kind + " style '" + name + "'", 'style.list with kind \'' + kind + "'");
	}
	function stylePath(s) {
		var parts = [s.name], p = s.parent;
		while (p && (p instanceof ParagraphStyleGroup || p instanceof CharacterStyleGroup || p instanceof ObjectStyleGroup || p instanceof TableStyleGroup || p instanceof CellStyleGroup)) {
			parts.unshift(p.name);
			p = p.parent;
		}
		return parts.join('/');
	}

	function applyTextFormat(d, t, p) {
		if (has(p, 'paragraphStyle')) t.applyParagraphStyle(styleNamed(d, 'paragraph', p.paragraphStyle), !!p.clearOverrides);
		if (has(p, 'characterStyle')) t.applyCharacterStyle(styleNamed(d, 'character', p.characterStyle));
		if (has(p, 'font')) {
			var f = app.fonts.itemByName(p.font + (has(p, 'fontStyle') ? '\t' + p.fontStyle : ''));
			t.appliedFont = has(p, 'fontStyle') && f.isValid ? f : p.font;
		}
		if (has(p, 'fontStyle')) t.fontStyle = p.fontStyle;
		if (has(p, 'size')) t.pointSize = p.size;
		if (has(p, 'leading')) t.leading = p.leading === 'auto' ? Leading.AUTO : p.leading;
		if (has(p, 'tracking')) t.tracking = p.tracking;
		if (has(p, 'kerning')) t.kerningMethod = p.kerning === 'metrics' ? '$ID/Metrics' : (p.kerning === 'optical' ? '$ID/Optical' : t.kerningMethod);
		if (has(p, 'color')) t.fillColor = colorFor(d, p.color);
		if (has(p, 'tint')) t.fillTint = p.tint;
		if (has(p, 'align')) {
			var a = {left: Justification.LEFT_ALIGN, center: Justification.CENTER_ALIGN, right: Justification.RIGHT_ALIGN,
				justify: Justification.LEFT_JUSTIFIED, justifyAll: Justification.FULLY_JUSTIFIED,
				towardsSpine: Justification.TO_BINDING_SIDE, awayFromSpine: Justification.AWAY_FROM_BINDING_SIDE}[p.align];
			if (!a) fail(kInvalidParams, 'align is left, center, right, justify, justifyAll, towardsSpine or awayFromSpine');
			t.justification = a;
		}
		var direct = {leftIndent: 'leftIndent', rightIndent: 'rightIndent', firstLineIndent: 'firstLineIndent', spaceBefore: 'spaceBefore',
			spaceAfter: 'spaceAfter', underline: 'underline', strikethrough: 'strikeThru', baselineShift: 'baselineShift', hyphenation: 'hyphenation'};
		for (var k in direct) if (has(p, k)) t[direct[k]] = p[k];
		if (has(p, 'case')) t.capitalization = {normal: Capitalization.NORMAL, allCaps: Capitalization.ALL_CAPS, smallCaps: Capitalization.SMALL_CAPS}[p['case']];
	}

	function fixBreaks(s) { return String(s).replace(/\r\n|\n/g, '\r'); }

	// ---------------------------------------------------------------- find / replace

	function withFind(p, fn) {
		var grep = !!p.grep;
		app.findTextPreferences = NothingEnum.NOTHING;
		app.changeTextPreferences = NothingEnum.NOTHING;
		app.findGrepPreferences = NothingEnum.NOTHING;
		app.changeGrepPreferences = NothingEnum.NOTHING;
		var opts = grep ? app.findChangeGrepOptions : app.findChangeTextOptions;
		opts.caseSensitive = !!p.caseSensitive;
		if (!grep) opts.wholeWord = !!p.wholeWord;
		var fp = grep ? app.findGrepPreferences : app.findTextPreferences;
		var cp = grep ? app.changeGrepPreferences : app.changeTextPreferences;
		fp.findWhat = need(p, 'find');
		try { return fn(grep, fp, cp); }
		finally {
			app.findTextPreferences = NothingEnum.NOTHING; app.changeTextPreferences = NothingEnum.NOTHING;
			app.findGrepPreferences = NothingEnum.NOTHING; app.changeGrepPreferences = NothingEnum.NOTHING;
		}
	}

	// ---------------------------------------------------------------- the commands

	var C = {};

	C['app.info'] = function () {
		var docs = [];
		for (var i = 0; i < app.documents.length; i++) docs.push(app.documents[i].name);
		return {app: 'InDesign', version: app.version, documents: docs, active: app.documents.length ? app.activeDocument.name : null};
	};

	// ---- documents

	C['document.list'] = function () {
		var out = [];
		for (var i = 0; i < app.documents.length; i++) {
			var d = app.documents[i], path = null;
			try { path = d.fullName.fsName; } catch (e) {}
			out.push({document: i, name: d.name, path: path, saved: d.saved, modified: d.modified, pages: d.pages.length, active: d == app.activeDocument});
		}
		return out;
	};

	C['document.info'] = function (p) {
		var d = doc(p), dp = d.documentPreferences, path = null;
		try { path = d.fullName.fsName; } catch (e) {}
		var m = d.marginPreferences;
		var pages = [];
		for (var i = 0; i < d.pages.length; i++) {
			var pg = d.pages[i];
			pages.push({name: pg.name, position: i + 1, spread: pg.parent.index + 1, side: String(pg.side).toLowerCase().replace('_hand', ''),
				parent: pg.appliedMaster ? pg.appliedMaster.name : 'None', items: pg.pageItems.length});
		}
		var layers = [];
		for (var j = 0; j < d.layers.length; j++) layers.push(d.layers[j].name);
		return {
			name: d.name, path: path, saved: d.saved, modified: d.modified,
			pageSize: {width: dp.pageWidth, height: dp.pageHeight}, facingPages: dp.facingPages,
			margins: {top: m.top, bottom: m.bottom, left: m.left, right: m.right, columns: m.columnCount, gutter: m.columnGutter},
			bleed: {top: dp.documentBleedTopOffset, bottom: dp.documentBleedBottomOffset, inside: dp.documentBleedInsideOrLeftOffset, outside: dp.documentBleedOutsideOrRightOffset},
			pages: pages, spreads: d.spreads.length, layers: layers, activeLayer: d.activeLayer.name,
			parents: d.masterSpreads.length, stories: d.stories.length, paragraphStyles: d.allParagraphStyles.length,
			characterStyles: d.allCharacterStyles.length, swatches: d.swatches.length, links: d.links.length
		};
	};

	C['document.new'] = function (p) {
		var preset = has(p, 'preset') ? app.documentPresets.itemByName(p.preset) : undefined;
		if (preset && !preset.isValid) fail(kNotFound, "no document preset '" + p.preset + "'");
		var d = app.documents.add(true, preset);
		if (!preset) {
			var dp = d.documentPreferences;
			dp.facingPages = !!p.facingPages;
			dp.pageWidth = has(p, 'width') ? p.width : 612;
			dp.pageHeight = has(p, 'height') ? p.height : 792;
			dp.pagesPerDocument = has(p, 'pages') ? p.pages : 1;
			if (has(p, 'bleed')) dp.documentBleedUniformSize = true, dp.documentBleedTopOffset = p.bleed;
			var mg = has(p, 'margins') ? p.margins : 36;
			var m = d.masterSpreads[0].pages;
			for (var i = 0; i < m.length; i++) {
				var mp = m[i].marginPreferences;
				if (typeof mg === 'number') { mp.top = mp.bottom = mp.left = mp.right = mg; }
				else { if (has(mg, 'top')) mp.top = mg.top; if (has(mg, 'bottom')) mp.bottom = mg.bottom; if (has(mg, 'inside')) mp.left = mg.inside; if (has(mg, 'outside')) mp.right = mg.outside; }
				if (has(p, 'columns')) mp.columnCount = p.columns;
				if (has(p, 'gutter')) mp.columnGutter = p.gutter;
			}
		}
		var index = 0;
		for (var k = 0; k < app.documents.length; k++) if (app.documents[k] == d) index = k;
		return {document: index, name: d.name, pages: d.pages.length};
	};

	C['document.open'] = function (p) {
		var f = File(need(p, 'path'));
		if (!f.exists) fail(kNotFound, 'no file at ' + p.path);
		var d = app.open(f, true, p.asCopy ? OpenOptions.OPEN_COPY : OpenOptions.DEFAULT_VALUE);
		var missingLinks = [];
		for (var i = 0; i < d.links.length; i++) if (d.links[i].status == LinkStatus.LINK_MISSING) missingLinks.push(d.links[i].name);
		var missingFonts = [];
		for (var j = 0; j < d.fonts.length; j++) if (d.fonts[j].status != FontStatus.INSTALLED) missingFonts.push(d.fonts[j].name.replace('\t', ' '));
		return {name: d.name, pages: d.pages.length, missingLinks: missingLinks, missingFonts: missingFonts};
	};

	C['document.activate'] = function (p) { var d = doc(p); app.activeDocument = d; return {name: d.name}; };

	C['document.save'] = function (p) {
		var d = doc(p);
		if (has(p, 'path')) {
			var f = File(p.path);
			d.save(f, /\.indt$/i.test(p.path));
		}
		else {
			if (!d.saved) fail(kInvalidParams, "this document has never been saved, so it needs a 'path'");
			d.save();
		}
		return {name: d.name, path: d.fullName.fsName};
	};

	C['document.close'] = function (p) {
		var d = doc(p), name = d.name;
		if (p.save) {
			if (!d.saved) fail(kInvalidParams, "this document has never been saved: save it with a path first (document.save)");
			d.close(SaveOptions.YES);
		}
		else d.close(SaveOptions.NO);
		return {closed: name, saved: !!p.save};
	};

	C['document.export'] = function (p) {
		var d = doc(p), path = need(p, 'path');
		var ext = (p.format || path.replace(/^.*\./, '')).toLowerCase();
		var f = File(path), pages = has(p, 'pages') && p.pages !== 'all' ? String(p.pages) : null;
		if (ext === 'pdf') {
			var pref = app.pdfExportPreferences;
			pref.pageRange = pages || PageRange.ALL_PAGES;
			pref.exportReaderSpreads = !!p.spreads;
			pref.useDocumentBleedWithPDF = !!p.bleed;
			var preset = has(p, 'pdfPreset') ? app.pdfExportPresets.itemByName(p.pdfPreset) : undefined;
			if (preset && !preset.isValid) fail(kNotFound, "no PDF preset '" + p.pdfPreset + "'");
			if (preset) d.exportFile(ExportFormat.PDF_TYPE, f, false, preset);
			else d.exportFile(ExportFormat.PDF_TYPE, f, false);
		}
		else if (ext === 'png' || ext === 'jpg' || ext === 'jpeg') {
			var png = ext === 'png';
			var ip = png ? app.pngExportPreferences : app.jpegExportPreferences;
			if (png) { ip.pngExportRange = pages ? PNGExportRangeEnum.EXPORT_RANGE : PNGExportRangeEnum.EXPORT_ALL; ip.transparentBackground = !!p.transparent; }
			else ip.jpegExportRange = pages ? ExportRangeOrAllPages.EXPORT_RANGE : ExportRangeOrAllPages.EXPORT_ALL;
			if (pages) ip.pageString = pages;
			ip.exportResolution = has(p, 'resolution') ? p.resolution : 144;
			ip.exportingSpread = !!p.spreads;
			ip.useDocumentBleeds = !!p.bleed;
			d.exportFile(png ? ExportFormat.PNG_FORMAT : ExportFormat.JPG, f, false);
		}
		else if (ext === 'idml') d.exportFile(ExportFormat.INDESIGN_MARKUP, f, false);
		else if (ext === 'epub') d.exportFile(ExportFormat.EPUB, f, false);
		else if (ext === 'html') d.exportFile(ExportFormat.HTML, f, false);
		else fail(kInvalidParams, "can't export '" + ext + "'", 'formats: pdf, png, jpg, idml, epub, html');
		return {path: f.fsName, format: ext};
	};

	C['document.preflight'] = function (p) {
		var d = doc(p);
		var profile = has(p, 'profile') ? app.preflightProfiles.itemByName(p.profile) : d.preflightOptions.preflightWorkingProfile;
		if (!profile || !profile.isValid) fail(kNotFound, "no preflight profile '" + p.profile + "'");
		var proc = app.preflightProcesses.add(d, profile);
		proc.waitForProcess(30);
		var results = proc.processResults, out = {profile: profile.name, errors: []};
		var aggregated = proc.aggregatedResults;   // [docName, profile, [[category, name, page, info], ...]]
		try {
			var rows = aggregated[2];
			for (var i = 0; i < rows.length && i < 300; i++) out.errors.push({category: rows[i][0], problem: rows[i][1], where: rows[i][2], info: rows[i][3]});
		} catch (e) { out.summary = results; }
		proc.remove();
		out.count = out.errors.length;
		return out;
	};

	// ---- pages and parents

	C['page.list'] = function (p) {
		var d = doc(p), out = [];
		for (var i = 0; i < d.pages.length; i++) {
			var pg = d.pages[i], b = pg.bounds;
			out.push({name: pg.name, position: i + 1, spread: pg.parent.index + 1, side: String(pg.side).toLowerCase().replace('_hand', ''),
				parent: pg.appliedMaster ? pg.appliedMaster.name : 'None', size: {width: b[3] - b[1], height: b[2] - b[0]}, items: pg.pageItems.length});
		}
		return out;
	};

	function masterNamed(d, name) {
		if (name === 'None' || name === 'none') return NothingEnum.NOTHING;
		var m = d.masterSpreads.itemByName(name);
		if (!m.isValid) fail(kNotFound, "no parent spread '" + name + "'", 'parent.list shows them');
		return m;
	}

	C['page.add'] = function (p) {
		var d = doc(p), count = has(p, 'count') ? p.count : 1, added = [];
		var after = has(p, 'after') ? pageOf(d, p.after) : d.pages[-1];
		for (var i = 0; i < count; i++) {
			var pg = d.pages.add(LocationOptions.AFTER, after);
			if (has(p, 'parent')) pg.appliedMaster = masterNamed(d, p.parent);
			added.push(pg.name);
			after = pg;
		}
		return {added: added, pages: d.pages.length};
	};

	function pageRange(d, spec) {
		var out = [], parts = String(spec).split(',');
		for (var i = 0; i < parts.length; i++) {
			var bits = parts[i].replace(/\s/g, '').split('-');
			if (bits.length === 1) out.push(pageOf(d, bits[0]));
			else {
				var a = pageOf(d, bits[0]).documentOffset, b = pageOf(d, bits[1]).documentOffset;
				for (var k = a; k <= b; k++) out.push(d.pages[k]);
			}
		}
		return out;
	}

	C['page.delete'] = function (p) {
		var d = doc(p), pages = pageRange(d, need(p, 'pages')), names = [];
		if (pages.length >= d.pages.length) fail(kInvalidParams, "can't delete every page");
		for (var i = pages.length - 1; i >= 0; i--) { names.unshift(pages[i].name); pages[i].remove(); }
		return {deleted: names, pages: d.pages.length};
	};

	C['page.move'] = function (p) {
		var d = doc(p), pg = pageOf(d, need(p, 'page'));
		if (String(p.after) === '0') pg.move(LocationOptions.AT_BEGINNING);
		else pg.move(LocationOptions.AFTER, pageOf(d, need(p, 'after')));
		return {name: pg.name, position: pg.documentOffset + 1};
	};

	C['page.set'] = function (p) {
		var d = doc(p), pg = pageOf(d, need(p, 'page'));
		if (has(p, 'parent')) pg.appliedMaster = masterNamed(d, p.parent);
		if (has(p, 'width') || has(p, 'height')) {
			var b = pg.bounds;
			pg.resize(CoordinateSpaces.INNER_COORDINATES, AnchorPoint.TOP_LEFT_ANCHOR, ResizeMethods.REPLACING_CURRENT_DIMENSIONS_WITH,
				[has(p, 'width') ? p.width : b[3] - b[1], has(p, 'height') ? p.height : b[2] - b[0]]);
		}
		var mp = pg.marginPreferences;
		if (has(p, 'margins')) {
			var m = p.margins;
			if (typeof m === 'number') mp.top = mp.bottom = mp.left = mp.right = m;
			else { if (has(m, 'top')) mp.top = m.top; if (has(m, 'bottom')) mp.bottom = m.bottom; if (has(m, 'left')) mp.left = m.left; if (has(m, 'right')) mp.right = m.right; }
		}
		if (has(p, 'columns')) mp.columnCount = p.columns;
		if (has(p, 'gutter')) mp.columnGutter = p.gutter;
		return {name: pg.name, parent: pg.appliedMaster ? pg.appliedMaster.name : 'None'};
	};

	C['page.go'] = function (p) { var d = doc(p), pg = pageOf(d, need(p, 'page')); app.activeWindow.activePage = pg; return {name: pg.name}; };

	C['parent.list'] = function (p) {
		var d = doc(p), out = [];
		for (var i = 0; i < d.masterSpreads.length; i++) {
			var m = d.masterSpreads[i], used = [];
			for (var j = 0; j < d.pages.length; j++) if (d.pages[j].appliedMaster == m) used.push(d.pages[j].name);
			var items = [];
			for (var k = 0; k < m.pageItems.length; k++) items.push(brief(m.pageItems[k].getElements()[0]));
			out.push({name: m.name, prefix: m.namePrefix, basedOn: m.appliedMaster ? m.appliedMaster.name : null, pages: m.pages.length, usedBy: used, items: items});
		}
		return out;
	};

	C['parent.create'] = function (p) {
		var d = doc(p), props = {};
		if (has(p, 'prefix')) props.namePrefix = p.prefix;
		if (has(p, 'name')) props.baseName = p.name;
		var m = d.masterSpreads.add(d.documentPreferences.facingPages ? 2 : 1, props);
		if (has(p, 'basedOn')) m.appliedMaster = masterNamed(d, p.basedOn);
		return {name: m.name};
	};

	C['parent.override'] = function (p) {
		var d = doc(p), pg = pageOf(d, need(p, 'page')), out = [];
		if (!pg.appliedMaster) fail(kInvalidParams, 'page ' + pg.name + ' has no parent');
		var items = pg.appliedMaster.pageItems.everyItem().getElements();
		for (var i = 0; i < items.length; i++) {
			if (has(p, 'ids')) { var want = false; for (var j = 0; j < p.ids.length; j++) if (p.ids[j] == items[i].id) want = true; if (!want) continue; }
			try { out.push(brief(items[i].override(pg))); } catch (e) {}
		}
		return out;
	};

	// ---- layers

	C['layer.list'] = function (p) {
		var d = doc(p), out = [];
		for (var i = 0; i < d.layers.length; i++) {
			var l = d.layers[i];
			out.push({id: l.id, name: l.name, visible: l.visible, locked: l.locked, printable: l.printable, items: l.pageItems.length, active: l == d.activeLayer});
		}
		return out;
	};

	C['layer.create'] = function (p) {
		var d = doc(p), l = d.layers.add({name: need(p, 'name')});
		if (has(p, 'above')) l.move(LocationOptions.BEFORE, layerFor(d, p.above));
		else if (has(p, 'below')) l.move(LocationOptions.AFTER, layerFor(d, p.below));
		if (has(p, 'color')) try { l.layerColor = UIColors[String(p.color).toUpperCase().replace(/ /g, '_')]; } catch (e) {}
		return {id: l.id, name: l.name};
	};

	C['layer.set'] = function (p) {
		var d = doc(p), l = layerFor(d, need(p, 'layer'));
		if (p['delete']) { var n = l.name; l.remove(); return {deleted: n}; }
		if (has(p, 'name')) l.name = p.name;
		if (has(p, 'visible')) l.visible = !!p.visible;
		if (has(p, 'locked')) l.locked = !!p.locked;
		if (has(p, 'printable')) l.printable = !!p.printable;
		if (p.active) d.activeLayer = l;
		if (has(p, 'above')) l.move(LocationOptions.BEFORE, layerFor(d, p.above));
		return {id: l.id, name: l.name, visible: l.visible, locked: l.locked};
	};

	// ---- page items

	C['item.tree'] = function (p) {
		var d = doc(p), container;
		if (has(p, 'parent')) container = masterNamed(d, p.parent);
		else if (has(p, 'spread')) { container = d.spreads[p.spread - 1]; if (!container || !container.isValid) fail(kNotFound, 'no spread ' + p.spread); }
		else container = pageOf(d, p.page);
		var depth = has(p, 'depth') ? p.depth : 3, out = [];
		var items = container.pageItems.everyItem().getElements();
		for (var i = 0; i < items.length; i++) {
			if (has(p, 'layer') && items[i].itemLayer.name !== p.layer) continue;
			out.push(describe(items[i], depth));
		}
		return out;
	};

	C['item.get'] = function (p) { var d = doc(p); return detail(findItem(d, need(p, 'id'))); };

	C['item.selection'] = function (p) {
		var d = doc(p), sel = app.selection;
		if (sel.length === 1 && sel[0].hasOwnProperty('parentStory') && !(sel[0].hasOwnProperty('geometricBounds'))) {
			var t = sel[0];
			return {text: {story: t.parentStory.id, from: t.index, count: t.length, contents: String(t.contents).substr(0, 2000)}};
		}
		var out = [], items = selectionItems(d);
		for (var i = 0; i < items.length; i++) out.push(brief(items[i]));
		return out;
	};

	C['item.select'] = function (p) {
		var d = doc(p), ids = has(p, 'ids') ? p.ids : [];
		if (!ids.length && !p.add) { app.select(NothingEnum.NOTHING); return {ids: []}; }
		var items = [];
		for (var i = 0; i < ids.length; i++) items.push(findItem(d, ids[i]));
		app.select(items, p.add ? SelectionOptions.ADD_TO : SelectionOptions.REPLACE_WITH);
		var now = [], sel = selectionItems(d);
		for (var j = 0; j < sel.length; j++) now.push(sel[j].id);
		return {ids: now};
	};

	C['item.set'] = function (p) {
		var d = doc(p), items = targets(d, p), out = [];
		for (var i = 0; i < items.length; i++) { applyLook(d, items[i], p); out.push(brief(items[i])); }
		return out;
	};

	C['item.bounds'] = function (p) {
		var d = doc(p), item = findItem(d, need(p, 'id')), page = pageFor(item) || activePage(d);
		var b = boundsOf(item, page);
		var nb = {x: has(p, 'x') ? p.x : b.x, y: has(p, 'y') ? p.y : b.y, width: has(p, 'width') ? p.width : b.width, height: has(p, 'height') ? p.height : b.height};
		if (p.scaleContent) {
			item.resize(CoordinateSpaces.INNER_COORDINATES, AnchorPoint.TOP_LEFT_ANCHOR, ResizeMethods.MULTIPLYING_CURRENT_DIMENSIONS_BY, [nb.width / b.width, nb.height / b.height]);
			item.move(undefined, [nb.x - boundsOf(item, page).x, nb.y - boundsOf(item, page).y]);
		}
		else item.geometricBounds = toGeometric(nb, page);
		return brief(item);
	};

	C['item.transform'] = function (p) {
		var d = doc(p), items = targets(d, p), out = [];
		var m = app.transformationMatrices.add();
		if (has(p, 'scale')) { var s = p.scale instanceof Array ? p.scale : [p.scale, p.scale]; m = m.scaleMatrix(s[0], s[1]); }
		if (has(p, 'shear')) m = m.shearMatrix(p.shear);
		if (has(p, 'rotate')) m = m.rotateMatrix(p.rotate);
		var origin = AnchorPoint.CENTER_ANCHOR;
		for (var i = 0; i < items.length; i++) {
			var it = items[i];
			if (has(p, 'scale') || has(p, 'shear') || has(p, 'rotate')) {
				if (has(p, 'origin')) {
					var page = pageFor(it) || activePage(d), o = pageOrigin(page);
					it.transform(CoordinateSpaces.PASTEBOARD_COORDINATES, [[p.origin[0] + o[0], p.origin[1] + o[1]], BoundingBoxLimits.GEOMETRIC_PATH_BOUNDS, CoordinateSpaces.SPREAD_COORDINATES], m);
				}
				else it.transform(CoordinateSpaces.PASTEBOARD_COORDINATES, origin, m);
			}
			if (has(p, 'translate')) it.move(undefined, [p.translate[0], p.translate[1]]);
			out.push(brief(it));
		}
		return out;
	};

	C['item.arrange'] = function (p) {
		var d = doc(p), items = targets(d, p), out = [];
		for (var i = 0; i < items.length; i++) {
			var it = items[i];
			if (p.order === 'front') it.bringToFront();
			else if (p.order === 'back') it.sendToBack();
			else if (p.order === 'forward') it.bringForward();
			else if (p.order === 'backward') it.sendBackward();
			if (has(p, 'layer')) it.itemLayer = layerFor(d, p.layer);
			out.push(brief(it));
		}
		return out;
	};

	C['item.align'] = function (p) {
		var d = doc(p), items = targets(d, p);
		var to = {selection: AlignDistributeBounds.ITEM_BOUNDS, margins: AlignDistributeBounds.MARGIN_BOUNDS, page: AlignDistributeBounds.PAGE_BOUNDS,
			spread: AlignDistributeBounds.SPREAD_BOUNDS, keyObject: AlignDistributeBounds.KEY_OBJECT}[p.to || 'selection'];
		var key = has(p, 'key') ? findItem(d, p.key) : undefined;
		if (has(p, 'align')) {
			var a = {left: AlignOptions.LEFT_EDGES, centerX: AlignOptions.HORIZONTAL_CENTERS, right: AlignOptions.RIGHT_EDGES,
				top: AlignOptions.TOP_EDGES, centerY: AlignOptions.VERTICAL_CENTERS, bottom: AlignOptions.BOTTOM_EDGES}[p.align];
			d.align(items, a, to, key);
		}
		if (has(p, 'distribute')) {
			var ds = {horizontal: DistributeOptions.HORIZONTAL_CENTERS, vertical: DistributeOptions.VERTICAL_CENTERS,
				spaceHorizontal: DistributeOptions.HORIZONTAL_SPACE, spaceVertical: DistributeOptions.VERTICAL_SPACE}[p.distribute];
			d.distribute(items, ds, to, undefined, undefined, key);
		}
		var out = [];
		for (var i = 0; i < items.length; i++) out.push(brief(items[i]));
		return out;
	};

	C['item.duplicate'] = function (p) {
		var d = doc(p), items = targets(d, p), out = [];
		for (var i = 0; i < items.length; i++) {
			var copy;
			if (has(p, 'page')) copy = items[i].duplicate(pageOf(d, p.page));
			else copy = items[i].duplicate(undefined, has(p, 'translate') ? p.translate : [0, 0]);
			out.push(brief(copy));
		}
		return out;
	};

	C['item.group'] = function (p) {
		var d = doc(p), items = targets(d, p);
		if (items.length < 2) fail(kInvalidParams, 'grouping needs at least two items');
		var g = items[0].parent.groups.add(items);
		if (has(p, 'name')) g.name = p.name;
		return describe(g, 1);
	};

	C['item.ungroup'] = function (p) {
		var d = doc(p), items = targets(d, p), out = [];
		for (var i = 0; i < items.length; i++) {
			if (!(items[i] instanceof Group)) fail(kInvalidParams, 'item ' + items[i].id + " isn't a group");
			var kids = items[i].pageItems.everyItem().getElements();
			items[i].ungroup();
			for (var k = 0; k < kids.length; k++) out.push(brief(kids[k]));
		}
		return out;
	};

	C['item.delete'] = function (p) {
		var d = doc(p), items = targets(d, p), ids = [];
		for (var i = 0; i < items.length; i++) { ids.push(items[i].id); items[i].remove(); }
		return {deleted: ids};
	};

	// Frame bounds, not outlines: InDesign keeps a frame's rectangle handy, and
	// for proofing (text into a photo, a caption over a button) that's the question.
	function visibleOn(item) {
		try { if (!item.visible || !item.itemLayer.visible) return false; } catch (e) { return false; }
		return true;
	}
	function gap(a, b) {   // distance between two {x, y, width, height}; negative = overlap depth
		var dx = Math.max(a.x - (b.x + b.width), b.x - (a.x + a.width));
		var dy = Math.max(a.y - (b.y + b.height), b.y - (a.y + a.height));
		return dx > 0 || dy > 0 ? Math.sqrt(Math.max(dx, 0) * Math.max(dx, 0) + Math.max(dy, 0) * Math.max(dy, 0)) : Math.max(dx, dy);
	}
	// z: id -> position in the spread's allPageItems, which runs front to back
	// (pageItems is creation order, not stacking order).
	function zOrder(spread) {
		var z = {}, all = spread.allPageItems;
		for (var i = 0; i < all.length; i++) z[all[i].id] = i;
		return z;
	}
	function overlapRow(a, b, page, z) {
		var ba = boundsOf(a, page), bb = boundsOf(b, page), g = gap(ba, bb);
		var row = {a: brief(a), b: brief(b), overlaps: g < 0, distance: g < 0 ? 0 : Math.round(g * 100) / 100};
		if (g < 0) {
			var w = Math.min(ba.x + ba.width, bb.x + bb.width) - Math.max(ba.x, bb.x);
			var h = Math.min(ba.y + ba.height, bb.y + bb.height) - Math.max(ba.y, bb.y);
			row.overlap = {width: Math.round(w * 100) / 100, height: Math.round(h * 100) / 100};
		}
		if (z[a.id] !== undefined && z[b.id] !== undefined) row.onTop = z[a.id] < z[b.id] ? 'a' : 'b';
		return row;
	}
	function topLevelOn(page) {
		var out = [], items = page.parent.pageItems.everyItem().getElements();
		for (var i = 0; i < items.length; i++) {
			if (!visibleOn(items[i])) continue;
			var pg = pageFor(items[i]);
			if (pg && pg == page) out.push(items[i]);
		}
		return out;
	}

	C['item.overlaps'] = function (p) {
		var d = doc(p), margin = has(p, 'margin') ? p.margin : 0, limit = has(p, 'limit') ? p.limit : 200, out = [];
		if (has(p, 'id')) {
			var item = findItem(d, p.id), page = pageFor(item);
			if (!page) fail(kInvalidParams, 'item ' + item.id + ' is on the pasteboard, not a page');
			var spreadItems = page.parent.pageItems.everyItem().getElements(), z = zOrder(page.parent);
			for (var i = 0; i < spreadItems.length && out.length < limit; i++) {
				var other = spreadItems[i];
				if (other.id === item.id || !visibleOn(other)) continue;
				var row = overlapRow(item, other, page, z);
				if (row.overlaps || row.distance <= margin) out.push(row);
			}
			return {item: brief(item), count: out.length, near: out};
		}
		var pg = pageOf(d, p.page), items = topLevelOn(pg), total = 0, zz = zOrder(pg.parent);
		for (var a = 0; a < items.length; a++) for (var b = a + 1; b < items.length; b++) {
			var A = items[a], B = items[b];
			if (p.textOnly && !(A instanceof TextFrame) && !(B instanceof TextFrame)) continue;
			var r = overlapRow(A, B, pg, zz);
			if (!(r.overlaps || r.distance <= margin)) continue;
			total++;
			if (out.length < limit) out.push(r);
		}
		return {page: pageName(pg), items: items.length, count: total, pairs: out};
	};

	C['item.fit'] = function (p) {
		var d = doc(p), items = targets(d, p);
		var fit = {contentToFrame: FitOptions.CONTENT_TO_FRAME, frameToContent: FitOptions.FRAME_TO_CONTENT, proportionally: FitOptions.PROPORTIONALLY,
			fillProportionally: FitOptions.FILL_PROPORTIONALLY, center: FitOptions.CENTER_CONTENT, contentAware: FitOptions.CONTENT_AWARE_FIT}[need(p, 'fit')];
		if (!fit) fail(kInvalidParams, 'fit is contentToFrame, frameToContent, proportionally, fillProportionally, center or contentAware');
		var out = [];
		for (var i = 0; i < items.length; i++) { items[i].fit(fit); out.push(brief(items[i])); }
		return out;
	};

	C['item.anchor'] = function (p) {
		var d = doc(p), item = findItem(d, need(p, 'id')), story = findItem(d, need(p, 'story'));
		if (!(story instanceof Story)) story = story.parentStory;
		var ip = story.insertionPoints[need(p, 'index')];
		item.anchoredObjectSettings.insertAnchoredObject(ip, p.position === 'aboveLine' ? AnchorPosition.ABOVE_LINE : (p.position === 'anchored' ? AnchorPosition.ANCHORED : AnchorPosition.INLINE_POSITION));
		return brief(item);
	};

	// ---- creating

	C['frame.text'] = function (p) {
		var r = newFrame(p, 'textFrames'), f = r.item;
		if (has(p, 'contents')) f.contents = fixBreaks(p.contents);
		if (has(p, 'paragraphStyle')) f.parentStory.texts[0].applyParagraphStyle(styleNamed(r.d, 'paragraph', p.paragraphStyle), true);
		var tp = f.textFramePreferences;
		if (has(p, 'columns')) tp.textColumnCount = p.columns;
		if (has(p, 'inset')) tp.insetSpacing = [p.inset, p.inset, p.inset, p.inset];
		if (has(p, 'verticalJustification')) tp.verticalJustification = {top: VerticalJustification.TOP_ALIGN, center: VerticalJustification.CENTER_ALIGN,
			bottom: VerticalJustification.BOTTOM_ALIGN, justify: VerticalJustification.JUSTIFY_ALIGN}[p.verticalJustification];
		if (has(p, 'autoSize')) tp.autoSizingType = {off: AutoSizingTypeEnum.OFF, heightOnly: AutoSizingTypeEnum.HEIGHT_ONLY,
			widthOnly: AutoSizingTypeEnum.WIDTH_ONLY, heightAndWidth: AutoSizingTypeEnum.HEIGHT_AND_WIDTH}[p.autoSize];
		var b = brief(f);
		b.story = f.parentStory.id;
		b.overflows = f.overflows;
		return b;
	};

	C['frame.rect'] = function (p) {
		var r = newFrame(p, 'rectangles');
		return brief(r.item);
	};

	C['frame.ellipse'] = function (p) { return brief(newFrame(p, 'ovals').item); };

	C['frame.polygon'] = function (p) {
		var d = doc(p), page = pageOf(d, p.page);
		need(p, 'x'); need(p, 'y'); need(p, 'width'); need(p, 'height');
		var item = page.polygons.add(layerFor(d, p.layer), has(p, 'sides') ? p.sides : 6, has(p, 'inset') ? p.inset : 0, {geometricBounds: toGeometric(p, page)});
		applyLook(d, item, p);
		return brief(item);
	};

	C['frame.line'] = function (p) {
		var d = doc(p), page = pageOf(d, p.page), o = pageOrigin(page);
		var line = page.graphicLines.add(layerFor(d, p.layer));
		line.paths[0].entirePath = [[need(p, 'x1') + o[0], need(p, 'y1') + o[1]], [need(p, 'x2') + o[0], need(p, 'y2') + o[1]]];
		line.strokeWeight = has(p, 'strokeWeight') ? p.strokeWeight : 1;
		line.strokeColor = has(p, 'stroke') ? colorFor(d, p.stroke) : d.swatches.itemByName('Black');
		if (has(p, 'name')) line.name = p.name;
		return brief(line);
	};

	C['frame.path'] = function (p) {
		var d = doc(p), page = pageOf(d, p.page), o = pageOrigin(page), pts = need(p, 'points'), path = [];
		for (var i = 0; i < pts.length; i++) {
			var a = pts[i];
			if (a instanceof Array) path.push([a[0] + o[0], a[1] + o[1]]);
			else {
				var at = [a.p[0] + o[0], a.p[1] + o[1]];
				var inn = a['in'] ? [a['in'][0] + o[0], a['in'][1] + o[1]] : at, out = a.out ? [a.out[0] + o[0], a.out[1] + o[1]] : at;
				path.push([inn, at, out]);
			}
		}
		var item = p.closed ? page.polygons.add(layerFor(d, p.layer)) : page.graphicLines.add(layerFor(d, p.layer));
		item.paths[0].entirePath = path;
		item.paths[0].pathType = p.closed ? PathType.CLOSED_PATH : PathType.OPEN_PATH;
		applyLook(d, item, p);
		if (!has(p, 'stroke') && !has(p, 'fill')) { item.strokeColor = d.swatches.itemByName('Black'); item.strokeWeight = 1; }
		return brief(item);
	};

	// ---- text

	C['text.get'] = function (p) {
		var d = doc(p), story = storyOf(d, p), all = String(story.contents);
		var from = has(p, 'from') ? p.from : 0, count = has(p, 'count') ? p.count : Math.min(all.length - from, 20000);
		var out = {story: story.id, length: all.length, contents: all.substr(from, count), overset: story.overflows, frames: []};
		for (var i = 0; i < story.textContainers.length; i++) out.frames.push(story.textContainers[i].id);
		if (p.styled !== false) {
			out.paragraphs = [];
			var paras = story.paragraphs.everyItem().getElements(), pos = 0;
			for (var k = 0; k < paras.length && k < 500; k++) {
				var para = paras[k], text = String(para.contents);
				var row = {index: k, from: pos, style: para.appliedParagraphStyle.name, text: text.substr(0, 200)};
				var runs = para.textStyleRanges.everyItem().getElements();
				if (runs.length > 1 || runs[0].appliedCharacterStyle.name !== '[None]') {
					row.runs = [];
					for (var r = 0; r < runs.length && r < 50; r++) {
						var run = runs[r];
						row.runs.push({text: String(run.contents).substr(0, 80), characterStyle: run.appliedCharacterStyle.name,
							font: run.appliedFont.name ? run.appliedFont.name.replace('\t', ' ') : String(run.appliedFont), size: run.pointSize});
					}
				}
				out.paragraphs.push(row);
				pos += text.length;
			}
		}
		var tables = story.tables;
		if (tables.length) { out.tables = []; for (var t = 0; t < tables.length; t++) out.tables.push({id: tables[t].id, rows: tables[t].bodyRowCount, columns: tables[t].columnCount}); }
		return out;
	};

	C['text.set'] = function (p) {
		var d = doc(p), story = storyOf(d, p), text = fixBreaks(need(p, 'contents'));
		if (p.at === 'start') story.insertionPoints[0].contents = text;
		else if (p.at === 'end') story.insertionPoints[-1].contents = text;
		else if (has(p, 'from')) {
			var n = has(p, 'count') ? p.count : 0;
			if (n === 0) story.insertionPoints[p.from].contents = text;
			else story.characters.itemByRange(p.from, p.from + n - 1).contents = text;
		}
		else story.contents = text;
		return {story: story.id, length: story.characters.length, overset: story.overflows};
	};

	C['text.format'] = function (p) {
		var d = doc(p), story = storyOf(d, p), ranges = textRange(story, p);
		for (var i = 0; i < ranges.length; i++) applyTextFormat(d, ranges[i], p);
		return {story: story.id, changed: ranges.length, overset: story.overflows};
	};

	C['text.find'] = function (p) {
		var d = doc(p), scope = has(p, 'id') ? storyOf(d, p) : d, limit = has(p, 'limit') ? p.limit : 200;
		return withFind(p, function (grep, fp) {
			if (has(p, 'paragraphStyle')) fp.appliedParagraphStyle = styleNamed(d, 'paragraph', p.paragraphStyle);
			var hits = grep ? scope.findGrep() : scope.findText(), out = [];
			for (var i = 0; i < hits.length && i < limit; i++) {
				var h = hits[i], frame = null, page = null;
				try { frame = h.parentTextFrames[0].id; page = pageName(h.parentTextFrames[0].parentPage); } catch (e) {}
				out.push({story: h.parentStory.id, frame: frame, page: page, from: h.index, count: h.length, text: String(h.contents).substr(0, 200)});
			}
			return {count: hits.length, hits: out};
		});
	};

	C['text.replace'] = function (p) {
		var d = doc(p), scope = has(p, 'id') ? storyOf(d, p) : d;
		return withFind(p, function (grep, fp, cp) {
			cp.changeTo = need(p, 'replace');
			if (has(p, 'paragraphStyle')) cp.appliedParagraphStyle = styleNamed(d, 'paragraph', p.paragraphStyle);
			if (has(p, 'characterStyle')) cp.appliedCharacterStyle = styleNamed(d, 'character', p.characterStyle);
			var changed = grep ? scope.changeGrep() : scope.changeText();
			return {changed: changed.length};
		});
	};

	C['text.thread'] = function (p) {
		var d = doc(p);
		if (has(p, 'unthread')) { var f = findItem(d, p.unthread); f.nextTextFrame = NothingEnum.NOTHING; return {unthreaded: f.id}; }
		var ids = need(p, 'ids');
		if (ids.length < 2) fail(kInvalidParams, 'threading needs at least two frames');
		for (var i = 0; i < ids.length - 1; i++) findItem(d, ids[i]).nextTextFrame = findItem(d, ids[i + 1]);
		var first = findItem(d, ids[0]);
		return {story: first.parentStory.id, frames: ids, overset: first.parentStory.overflows};
	};

	C['text.place'] = function (p) {
		var d = doc(p), f = File(need(p, 'path'));
		if (!f.exists) fail(kNotFound, 'no file at ' + p.path);
		app.wordRTFImportPreferences.removeFormatting = p.keepFormatting === false;
		var frame;
		if (has(p, 'id')) frame = findItem(d, p.id);
		else {
			var page = pageOf(d, p.page), m = page.marginPreferences, b = page.bounds;
			var box = has(p, 'x') ? p : {x: m.left, y: m.top, width: (b[3] - b[1]) - m.left - m.right, height: (b[2] - b[0]) - m.top - m.bottom};
			frame = page.textFrames.add({geometricBounds: toGeometric(box, page)});
		}
		frame.place(f);
		if (p.autoflow) {
			var story = frame.parentStory, guard = 0;
			while (story.overflows && guard++ < 500) {
				var last = story.textContainers[story.textContainers.length - 1];
				var np = d.pages.add(LocationOptions.AFTER, last.parentPage);
				var nf = np.textFrames.add({geometricBounds: last.geometricBounds});
				last.nextTextFrame = nf;
			}
		}
		return {story: frame.parentStory.id, frame: frame.id, frames: frame.parentStory.textContainers.length, overset: frame.parentStory.overflows};
	};

	C['text.overset'] = function (p) {
		var d = doc(p), out = [];
		for (var i = 0; i < d.stories.length; i++) {
			var s = d.stories[i];
			if (!s.overflows) continue;
			var last = s.textContainers[s.textContainers.length - 1], shown = 0;
			for (var k = 0; k < s.textContainers.length; k++) try { shown += s.textContainers[k].characters.length; } catch (e) {}
			out.push({story: s.id, frame: last.id, page: pageName(pageFor(last)), overset: s.characters.length - shown});
		}
		return out;
	};

	C['text.outline'] = function (p) {
		var d = doc(p), f = findItem(d, need(p, 'id'));
		var made = f.createOutlines(true), out = [];
		for (var i = 0; i < made.length; i++) out.push(brief(made[i]));
		return out;
	};

	C['font.list'] = function (p) {
		var src = p.used ? doc(p).fonts : app.fonts, out = {}, n = 0, list = [];
		var all = src.everyItem().getElements();
		for (var i = 0; i < all.length; i++) {
			var f = all[i], fam = f.fontFamily, sty = f.fontStyleName;
			if (has(p, 'search') && (fam + ' ' + sty).toLowerCase().indexOf(String(p.search).toLowerCase()) < 0) continue;
			if (!out[fam]) { out[fam] = {family: fam, styles: [], missing: false}; list.push(out[fam]); }
			out[fam].styles.push(sty);
			if (f.status != FontStatus.INSTALLED) out[fam].missing = true;
			if (++n > 3000) break;
		}
		return list;
	};

	// ---- styles and swatches

	C['style.list'] = function (p) {
		var d = doc(p), kind = need(p, 'kind');
		var col = {paragraph: d.allParagraphStyles, character: d.allCharacterStyles, object: d.allObjectStyles, table: d.allTableStyles, cell: d.allCellStyles}[kind];
		if (!col) fail(kInvalidParams, 'kind is paragraph, character, object, table or cell');
		var out = [];
		for (var i = 0; i < col.length; i++) {
			var s = col[i], row = {name: s.name, path: stylePath(s)};
			try { row.basedOn = s.basedOn ? s.basedOn.name : null; } catch (e) {}
			if (p.details) {
				try { row.font = s.appliedFont.name ? s.appliedFont.name.replace('\t', ' ') : String(s.appliedFont); row.size = s.pointSize; row.leading = String(s.leading);
					row.color = colorName(s.fillColor); row.spaceBefore = s.spaceBefore; row.spaceAfter = s.spaceAfter; } catch (e) {}
				try { row.nextStyle = s.nextStyle.name; } catch (e) {}
			}
			out.push(row);
		}
		return out;
	};

	C['style.create'] = function (p) {
		var d = doc(p), kind = need(p, 'kind'), name = need(p, 'name');
		var holder = d, groupPath = has(p, 'group') ? String(p.group).split('/') : [];
		var groupsKey = {paragraph: 'paragraphStyleGroups', character: 'characterStyleGroups', object: 'objectStyleGroups'}[kind];
		var stylesKey = {paragraph: 'paragraphStyles', character: 'characterStyles', object: 'objectStyles'}[kind];
		if (!stylesKey) fail(kInvalidParams, 'kind is paragraph, character or object');
		for (var g = 0; g < groupPath.length; g++) {
			var grp = holder[groupsKey].itemByName(groupPath[g]);
			holder = grp.isValid ? grp : holder[groupsKey].add({name: groupPath[g]});
		}
		var s = holder[stylesKey].itemByName(name);
		if (!s.isValid) s = holder[stylesKey].add({name: name});
		if (has(p, 'basedOn')) s.basedOn = styleNamed(d, kind, p.basedOn);
		if (has(p, 'nextStyle')) s.nextStyle = styleNamed(d, 'paragraph', p.nextStyle);
		if (has(p, 'from')) {
			var src = findItem(d, p.from);
			if (kind === 'object') s.properties = {fillColor: src.fillColor, strokeColor: src.strokeColor, strokeWeight: src.strokeWeight};
			else {
				var t = src instanceof Story ? src.texts[0] : src.parentStory.paragraphs[0];
				var props = ['appliedFont', 'fontStyle', 'pointSize', 'leading', 'tracking', 'fillColor', 'justification', 'spaceBefore', 'spaceAfter', 'firstLineIndent', 'leftIndent'];
				for (var k = 0; k < props.length; k++) try { s[props[k]] = t[props[k]]; } catch (e) {}
			}
		}
		if (has(p, 'settings')) {
			if (kind === 'object') applyLook(d, s, p.settings);
			else applyTextFormat(d, s, p.settings);
		}
		return {kind: kind, name: s.name, path: stylePath(s)};
	};

	C['style.apply'] = function (p) {
		var d = doc(p), kind = need(p, 'kind'), style = styleNamed(d, kind, need(p, 'name'));
		if (kind === 'object') {
			var items = targets(d, p), out = [];
			for (var i = 0; i < items.length; i++) { items[i].applyObjectStyle(style, !!p.clearOverrides); out.push(brief(items[i])); }
			return out;
		}
		var story = storyOf(d, p), ranges = textRange(story, p);
		for (var k = 0; k < ranges.length; k++) {
			if (kind === 'paragraph') ranges[k].applyParagraphStyle(style, !!p.clearOverrides);
			else ranges[k].applyCharacterStyle(style);
		}
		return {story: story.id, changed: ranges.length, style: style.name};
	};

	C['style.delete'] = function (p) {
		var d = doc(p), kind = need(p, 'kind'), style = styleNamed(d, kind, need(p, 'name'));
		var repl = has(p, 'replaceWith') ? styleNamed(d, kind, p.replaceWith) : undefined;
		var n = style.name;
		style.remove(repl);
		return {deleted: n};
	};

	C['swatch.list'] = function (p) {
		var d = doc(p), out = [];
		for (var i = 0; i < d.swatches.length; i++) {
			var s = d.swatches[i], row = {name: s.name, type: typeOf(s)};
			try { row.model = String(s.model).toLowerCase(); row.space = String(s.space).toLowerCase(); row.values = s.colorValue; } catch (e) {}
			try { if (s instanceof Tint) { row.tintOf = s.baseColor.name; row.tint = s.tintValue; } } catch (e) {}
			try { if (s instanceof Gradient) row.stops = s.gradientStops.length; } catch (e) {}
			out.push(row);
		}
		return out;
	};

	C['swatch.create'] = function (p) {
		var d = doc(p), name = need(p, 'name');
		if (has(p, 'tintOf')) {
			var base = d.swatches.itemByName(p.tintOf);
			if (!base.isValid) fail(kNotFound, "no swatch '" + p.tintOf + "'");
			var t = d.tints.add(base, {tintValue: has(p, 'tint') ? p.tint : 50});
			return {name: t.name};
		}
		if (has(p, 'gradient')) {
			var g = d.gradients.add({name: name, type: p.radial ? GradientType.RADIAL : GradientType.LINEAR});
			var stops = p.gradient;
			while (g.gradientStops.length < stops.length) g.gradientStops.add();
			for (var i = 0; i < stops.length; i++) { g.gradientStops[i].stopColor = colorFor(d, stops[i].color); g.gradientStops[i].location = has(stops[i], 'location') ? stops[i].location : i * 100 / (stops.length - 1); }
			return {name: g.name};
		}
		var space = ColorSpace.CMYK, values = p.cmyk;
		if (has(p, 'rgb')) { space = ColorSpace.RGB; values = p.rgb; }
		if (has(p, 'hex')) { var h = p.hex.replace('#', ''); space = ColorSpace.RGB; values = [parseInt(h.substr(0, 2), 16), parseInt(h.substr(2, 2), 16), parseInt(h.substr(4, 2), 16)]; }
		if (!values) fail(kInvalidParams, 'give cmyk, rgb, hex, tintOf or gradient');
		var c = d.colors.itemByName(name);
		if (!c.isValid) c = d.colors.add({name: name});
		c.properties = {model: p.spot ? ColorModel.SPOT : ColorModel.PROCESS, space: space, colorValue: values};
		return {name: c.name, model: p.spot ? 'spot' : 'process'};
	};

	C['swatch.delete'] = function (p) {
		var d = doc(p), s = d.swatches.itemByName(need(p, 'name'));
		if (!s.isValid) fail(kNotFound, "no swatch '" + p.name + "'");
		var repl = has(p, 'replaceWith') ? colorFor(d, p.replaceWith) : undefined;
		s.remove(repl);
		return {deleted: p.name};
	};

	// ---- images and links

	C['image.place'] = function (p) {
		var d = doc(p), f = File(need(p, 'path'));
		if (!f.exists) fail(kNotFound, 'no file at ' + p.path);
		if (has(p, 'pdfPage')) { app.pdfPlacePreferences.pageNumber = p.pdfPage; try { app.importedPageAttributes.pageNumber = p.pdfPage; } catch (e) {} }
		var frame, placed;
		if (has(p, 'id')) { frame = findItem(d, p.id); placed = frame.place(f)[0]; }
		else {
			var page = pageOf(d, p.page), o = pageOrigin(page);
			placed = page.place(f, [(has(p, 'x') ? p.x : 0) + o[0], (has(p, 'y') ? p.y : 0) + o[1]], layerFor(d, p.layer))[0];
			frame = placed.parent;
			if (has(p, 'width') && has(p, 'height')) frame.geometricBounds = toGeometric({x: p.x || 0, y: p.y || 0, width: p.width, height: p.height}, page);
		}
		var fit = p.fit || (has(p, 'id') || has(p, 'width') ? 'proportionally' : 'none');
		var fits = {proportionally: FitOptions.PROPORTIONALLY, fillProportionally: FitOptions.FILL_PROPORTIONALLY, contentToFrame: FitOptions.CONTENT_TO_FRAME, frameToContent: FitOptions.FRAME_TO_CONTENT};
		if (fits[fit]) frame.fit(fits[fit]);
		if (fit === 'proportionally' || fit === 'fillProportionally') frame.fit(FitOptions.CENTER_CONTENT);
		if (p.link === false) try { placed.itemLink.unlink(); } catch (e) {}
		var b = brief(frame);
		b.image = {type: typeOf(placed)};
		try { b.image.effectivePpi = placed.effectivePpi; } catch (e) {}
		return b;
	};

	C['link.list'] = function (p) {
		var d = doc(p), out = [];
		for (var i = 0; i < d.links.length; i++) {
			var l = d.links[i], st = String(l.status), row = {id: l.id, name: l.name, file: l.filePath, status: st.toLowerCase().replace('link_', '').replace('normal', 'ok')};
			try { var g = l.parent; row.frame = g.parent.id; row.page = pageName(pageFor(g.parent)); row.effectivePpi = g.effectivePpi; row.scale = [g.horizontalScale, g.verticalScale]; } catch (e) {}
			if (p.problems) {
				var low = row.effectivePpi && row.effectivePpi[0] < 150;
				if (l.status == LinkStatus.NORMAL && !low) continue;
			}
			out.push(row);
		}
		return out;
	};

	C['link.update'] = function (p) {
		var d = doc(p), out = [];
		if (has(p, 'relink')) {
			if (!has(p, 'ids') || p.ids.length !== 1) fail(kInvalidParams, 'relink needs exactly one link id in ids');
			var l = d.links.itemByID(p.ids[0]);
			if (!l.isValid) fail(kNotFound, 'no link ' + p.ids[0]);
			l.relink(File(p.relink));
			return [{id: l.id, name: l.name, status: String(l.status)}];
		}
		for (var i = 0; i < d.links.length; i++) {
			var k = d.links[i];
			if (has(p, 'ids')) { var want = false; for (var j = 0; j < p.ids.length; j++) if (p.ids[j] == k.id) want = true; if (!want) continue; }
			if (k.status == LinkStatus.LINK_OUT_OF_DATE) { k.update(); out.push({id: k.id, name: k.name}); }
		}
		return {updated: out};
	};

	// ---- tables

	C['table.create'] = function (p) {
		var d = doc(p), rows = need(p, 'rows'), cols = need(p, 'columns'), ip;
		if (has(p, 'id')) {
			var story = storyOf(d, p);
			ip = has(p, 'at') ? story.insertionPoints[p.at] : story.insertionPoints[-1];
		}
		else {
			need(p, 'x'); need(p, 'y'); need(p, 'width'); need(p, 'height');
			var page = pageOf(d, p.page);
			ip = page.textFrames.add({geometricBounds: toGeometric(p, page)}).insertionPoints[0];
		}
		var t = ip.tables.add({bodyRowCount: rows, columnCount: cols});
		if (has(p, 'headerRows')) t.headerRowCount = p.headerRows;
		if (has(p, 'contents')) fillTable(t, p.contents);
		if (has(p, 'tableStyle')) t.appliedTableStyle = styleNamed(d, 'table', p.tableStyle);
		return {id: t.id, story: t.parent.parentStory ? t.parent.parentStory.id : null, rows: t.rows.length, columns: t.columnCount};
	};

	function fillTable(t, contents) {
		for (var r = 0; r < contents.length && r < t.rows.length; r++)
			for (var c = 0; c < contents[r].length && c < t.columnCount; c++) t.rows[r].cells[c].contents = fixBreaks(contents[r][c]);
	}

	function tableOf(d, id) {
		id = Number(id);
		for (var i = 0; i < d.stories.length; i++) {
			var t = d.stories[i].tables.itemByID(id);
			if (t.isValid) return t.getElements()[0];
		}
		fail(kNotFound, 'no table ' + id, 'text.get lists a story\'s tables');
	}

	C['table.get'] = function (p) {
		var d = doc(p), t = tableOf(d, need(p, 'id')), rows = [];
		for (var r = 0; r < t.rows.length && r < 500; r++) {
			var row = [];
			for (var c = 0; c < t.rows[r].cells.length; c++) row.push(String(t.rows[r].cells[c].contents));
			rows.push(row);
		}
		return {id: t.id, rows: t.rows.length, columns: t.columnCount, headerRows: t.headerRowCount, footerRows: t.footerRowCount,
			style: t.appliedTableStyle ? t.appliedTableStyle.name : null, contents: rows};
	};

	C['table.set'] = function (p) {
		var d = doc(p), t = tableOf(d, need(p, 'id')), i;
		if (has(p, 'addRows')) for (i = 0; i < p.addRows; i++) t.rows.add();
		if (has(p, 'addColumns')) for (i = 0; i < p.addColumns; i++) t.columns.add();
		if (has(p, 'deleteRows')) { var dr = p.deleteRows.slice().sort(function (a, b) { return b - a; }); for (i = 0; i < dr.length; i++) t.rows[dr[i]].remove(); }
		if (has(p, 'deleteColumns')) { var dc = p.deleteColumns.slice().sort(function (a, b) { return b - a; }); for (i = 0; i < dc.length; i++) t.columns[dc[i]].remove(); }
		if (has(p, 'contents')) fillTable(t, p.contents);
		if (has(p, 'cells')) for (i = 0; i < p.cells.length; i++) {
			var spec = p.cells[i], cell = t.rows[spec.row].cells[spec.column];
			if (!cell.isValid) fail(kNotFound, 'no cell at row ' + spec.row + ', column ' + spec.column);
			if (has(spec, 'contents')) cell.contents = fixBreaks(spec.contents);
			if (has(spec, 'fill')) cell.fillColor = colorFor(d, spec.fill);
			if (has(spec, 'cellStyle')) cell.appliedCellStyle = styleNamed(d, 'cell', spec.cellStyle);
			if (has(spec, 'paragraphStyle')) cell.texts[0].applyParagraphStyle(styleNamed(d, 'paragraph', spec.paragraphStyle), true);
		}
		if (has(p, 'columnWidths')) for (i = 0; i < p.columnWidths.length && i < t.columnCount; i++) t.columns[i].width = p.columnWidths[i];
		if (has(p, 'rowHeights')) for (i = 0; i < p.rowHeights.length && i < t.rows.length; i++) t.rows[i].height = p.rowHeights[i];
		if (has(p, 'merge')) { var m = p.merge; t.rows[m.row].cells[m.column].merge(t.rows[m.row + (m.rows || 1) - 1].cells[m.column + (m.columns || 1) - 1]); }
		if (has(p, 'headerRows')) t.headerRowCount = p.headerRows;
		if (has(p, 'footerRows')) t.footerRowCount = p.footerRows;
		if (has(p, 'tableStyle')) t.appliedTableStyle = styleNamed(d, 'table', p.tableStyle);
		return {id: t.id, rows: t.rows.length, columns: t.columnCount};
	};

	// ---- view

	C['view.screenshot'] = function (p) {
		var d = doc(p), page = pageOf(d, p.page);
		var f = has(p, 'path') ? File(p.path) : File(Folder.temp + '/peppy-shot-' + (new Date().getTime()) + '.png');
		var ip = app.pngExportPreferences;
		ip.pngExportRange = PNGExportRangeEnum.EXPORT_RANGE;
		ip.pageString = page.parent instanceof MasterSpread ? page.parent.name : page.name;
		ip.exportResolution = has(p, 'resolution') ? p.resolution : 72;
		ip.exportingSpread = !!p.spread;
		ip.transparentBackground = false;
		ip.useDocumentBleeds = false;
		d.exportFile(ExportFormat.PNG_FORMAT, f, false);
		return {path: f.fsName, page: pageName(page), temporary: !has(p, 'path')};
	};

	C['view.set'] = function (p) {
		var w = app.activeWindow;
		if (!w) fail(kUnavailable, 'no document window');
		if (has(p, 'page')) w.activePage = pageOf(doc(p), p.page);
		if (has(p, 'id')) { var it = findItem(doc(p), p.id); app.select(it); w.zoom(ZoomOptions.FIT_PAGE); }
		if (has(p, 'fit')) w.zoom({page: ZoomOptions.FIT_PAGE, spread: ZoomOptions.FIT_SPREAD, pasteboard: ZoomOptions.SHOW_PASTEBOARD}[p.fit]);
		if (has(p, 'zoom')) w.zoomPercentage = p.zoom;
		if (has(p, 'mode')) w.screenMode = {normal: ScreenModeOptions.PREVIEW_OFF, preview: ScreenModeOptions.PREVIEW_TO_PAGE, bleed: ScreenModeOptions.PREVIEW_TO_BLEED,
			slug: ScreenModeOptions.PREVIEW_TO_SLUG, presentation: ScreenModeOptions.PRESENTATION_PREVIEW}[p.mode];
		return {zoom: w.zoomPercentage, page: w.activePage.name};
	};

	// ---- history (runs outside any undo step of Peppy's own)

	function undoRedo(p, undo) {
		var d = doc(p), steps = has(p, 'steps') ? p.steps : 1, names = [];
		for (var i = 0; i < steps; i++) {
			var name = undo ? d.undoName : d.redoName;
			if (!name) break;
			if (undo) d.undo(); else d.redo();
			names.push(name);
		}
		if (!names.length) fail(kHost, undo ? 'nothing to undo in ' + d.name : 'nothing to redo in ' + d.name);
		var out = {document: d.name};
		out[undo ? 'undone' : 'redone'] = names;
		return out;
	}
	C['history.undo'] = function (p) { return undoRedo(p, true); };
	C['history.redo'] = function (p) { return undoRedo(p, false); };

	// ---- menu actions

	C['action.list'] = function (p) {
		var out = [], all = app.menuActions.everyItem().getElements();
		for (var i = 0; i < all.length; i++) {
			var a = all[i];
			if (has(p, 'search') && a.name.toLowerCase().indexOf(String(p.search).toLowerCase()) < 0) continue;
			out.push({id: a.id, name: a.name, area: a.area, enabled: a.enabled});
			if (out.length >= 300) break;
		}
		return out;
	};

	C['action.run'] = function (p) {
		var a = has(p, 'id') ? app.menuActions.itemByID(p.id) : app.menuActions.itemByName(need(p, 'name'));
		if (!a.isValid) fail(kNotFound, 'no such menu action', 'action.list finds them');
		if (/\.\.\.$|\u2026$/.test(a.name)) fail(kInvalidParams, "'" + a.name + "' opens a dialog, so Peppy won't run it");
		if (!a.enabled) fail(kUnavailable, "'" + a.name + "' isn't available right now");
		a.invoke();
		return {ran: a.name};
	};

	// ---------------------------------------------------------------- the entry point

	function run(method, json) {
		var saved = {};
		try {
			saved.units = app.scriptPreferences.measurementUnit;
			saved.interaction = app.scriptPreferences.userInteractionLevel;
			app.scriptPreferences.measurementUnit = MeasurementUnits.POINTS;
			app.scriptPreferences.userInteractionLevel = UserInteractionLevels.NEVER_INTERACT;
			var fn = C[method];
			if (!fn) fail(-32601, method + " isn't built yet in this version of Peppy", 'use another command, or script.run as a last resort');
			var result = fn(json ? parse(json) : {});
			return stringify({result: result === undefined ? null : result});
		}
		catch (e) {
			if (e && e.peppy) return stringify({error: {code: e.code, message: e.message, hint: e.hint}});
			var msg = e && e.message ? e.message : String(e);
			return stringify({error: {code: kHost, message: 'InDesign: ' + msg + (e && e.line ? ' (peppy.jsx line ' + e.line + ')' : '')}});
		}
		finally {
			try { app.scriptPreferences.measurementUnit = saved.units; app.scriptPreferences.userInteractionLevel = saved.interaction; } catch (e) {}
		}
	}

	return {run: run, commands: C};
})();
