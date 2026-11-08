package states.loadingState.backend;

import crowplexus.hscript.Expr;
import crowplexus.hscript.Tools;

import luahscript.exprs.LuaExpr;
import luahscript.LuaTools;
import luahscript.exprs.LuaConst;

class ScriptExprTools {
	public static function hx_getValue(e:Expr):Dynamic {
		if(e == null) return null;
		return switch(Tools.expr(e)) {
			case EParent(e): hx_getValue(e);
			case EConst(c):
				switch(c) {
					case CString(s): s;
					case CInt(i): i;
					case CFloat(f): f;
					#if !haxe3
					case CInt32(i): i;
					#end
					case _:
						null;
				}
			case _:
				null;
		}
	}

	public static function hx_searchCallback(e:Expr, ?func:Expr->Array<Expr>->Void) {
		if(e == null) return;
		switch(Tools.expr(e)) {
			case EIgnore(_), EIdent(_), EImport(_, _), EClass(_, _, _, _, _), EEnum(_, _, _), ETypedef(_, _), EUsing(_):
			case EBreak, EContinue:
			case EConst(c):
				switch(c) {
					case CString(_, sm):
						if(sm != null) for(s in sm) {
							hx_searchCallback(s.e, func);
						}
					case _:
				}
			case EVar(_, _, _, e):
				hx_searchCallback(e, func);
			case EParent(e):
				hx_searchCallback(e, func);
			case EBlock(e):
				for(e in e) hx_searchCallback(e, func);
			case EField(e, _, _):
				hx_searchCallback(e, func);
			case EBinop(_, e1, e2):
				hx_searchCallback(e1, func);
				hx_searchCallback(e2, func);
			case EUnop(_, _, e):
				hx_searchCallback(e, func);
			case ECall(e, params):
				if(func != null) {
					hx_recursion(e, function(e:Expr) {
						func(e, params);
					});
				}
				hx_searchCallback(e, func);
				for(p in params) {
					hx_searchCallback(p, func);
				}
			case EIf(cond, e1, e2):
				hx_searchCallback(cond, func);
				hx_searchCallback(e1, func);
				hx_searchCallback(e2, func);
			case EWhile(cond, e):
				hx_searchCallback(cond, func);
				hx_searchCallback(e, func);
			case EFor(_, it, e):
				hx_searchCallback(it, func);
				hx_searchCallback(e, func);
			case EForGen(it, e):
				hx_searchCallback(it, func);
				hx_searchCallback(e, func);
			case EFunction(_, e, _):
				hx_searchCallback(e, func);
			case EReturn(e):
				hx_searchCallback(e, func);
			case EArray(e, index):
				hx_searchCallback(e, func);
				hx_searchCallback(index, func);
			case EArrayDecl(e):
				for(ass in e) {
					hx_searchCallback(ass, func);
				}
			case ENew(_, params):
				for(p in params) {
					hx_searchCallback(p, func);
				}
			case EThrow(e):
				hx_searchCallback(e, func);
			case ETry(e, _, _, ecatch):
				hx_searchCallback(e, func);
				hx_searchCallback(ecatch, func);
			case EObject(fl):
				for(f in fl) {
					hx_searchCallback(f.e, func);
				}
			case ETernary(cond, e1, e2):
				hx_searchCallback(cond, func);
				hx_searchCallback(e1, func);
				hx_searchCallback(e2, func);
			case ESwitch(e, cases, defaultExpr):
				hx_searchCallback(e, func);
				for(c in cases) {
					for(e in c.values) hx_searchCallback(e, func);
					hx_searchCallback(c.ifExpr, func);
					hx_searchCallback(c.expr, func);
				}
				hx_searchCallback(defaultExpr, func);
			case EDoWhile(cond, e):
				hx_searchCallback(e, func);
				hx_searchCallback(cond, func);
			case EMeta(_, args, e):
				for(ae in args) hx_searchCallback(ae, func);
				hx_searchCallback(e, func);
			case ECheckType(e, _):
				hx_searchCallback(e, func);
			case ECast(e, _):
				hx_searchCallback(e, func);
		}
	}

	public static function lua_getValue(e:LuaExpr):Dynamic {
		if(e == null) return null;
		return switch(e.expr) {
			case EParent(e):
				lua_getValue(e);
			case EConst(c):
				switch(c) {
					case CString(ah, _): ah;
					case CInt(i): i;
					case CFloat(f): f;
					case CTripleDot: null;
				};
			case _:
				null;
		}
	}

	public static function lua_searchCallback(e:LuaExpr, ?func:LuaExpr->Array<LuaExpr>->Void) {
		if(e == null) return;
		switch(e.expr) {
			case EConst(_), EIdent(_), EGoto(_), ELabel(_):
			case EBreak, EContinue, EIgnore:
			case EParent(e):
				lua_searchCallback(e, func);
			case EField(e, _):
				lua_searchCallback(e, func);
			case ELocal(e):
				lua_searchCallback(e, func);
			case EBinop(_, e1, e2):
				lua_searchCallback(e1, func);
				lua_searchCallback(e2, func);
			case EPrefix(_, e):
				lua_searchCallback(e, func);
			case ECall(e, params):
				if(func != null) {
					LuaTools.recursion(e, function(e:LuaExpr) {
						func(e, params);
					});
				}
				lua_searchCallback(e, func);
				for(p in params) lua_searchCallback(p, func);
			case ETd(ae):
				for(e in ae) lua_searchCallback(e, func);
			case EAnd(ae):
				for(e in ae) lua_searchCallback(e, func);
			case EIf(cond, body, eis, eel):
				lua_searchCallback(cond, func);
				lua_searchCallback(body, func);
				if(eis != null) for(e in eis) {
					lua_searchCallback(e.cond, func);
					lua_searchCallback(e.body, func);
				}
				if(eel != null) lua_searchCallback(eel, func);
			case ERepeat(body, cond):
				lua_searchCallback(body, func);
				lua_searchCallback(cond, func);
			case EWhile(cond, e):
				lua_searchCallback(cond, func);
				lua_searchCallback(e, func);
			case EForNum(_, body, start, end, step):
				lua_searchCallback(body, func);
				lua_searchCallback(start, func);
				lua_searchCallback(end, func);
				if(step != null) lua_searchCallback(step, func);
			case EForGen(body, iterator, _):
				lua_searchCallback(iterator, func);
				lua_searchCallback(body, func);
			case EFunction(_, e):
				lua_searchCallback(e, func);
			case EReturn(e):
				lua_searchCallback(e, func);
			case EArray(e, index):
				lua_searchCallback(e, func);
				lua_searchCallback(index, func);
			case ETable(fl):
				for(fi in fl) {
					if(fi.key != null) lua_searchCallback(fi.key, func);
					lua_searchCallback(fi.v, func);
				}
		}
	}

	public static function hx_recursion(e:Expr, f:Expr->Void) {
		switch(Tools.expr(e)) {
			case EParent(e):
				hx_recursion(e, f);
			case _:
				f(e);
		}
	}
}

