'use strict';
var fs=require('fs'),path=require('path'),crypto=require('crypto');
var source=process.argv[2],out=process.argv[3];
function read(name){return fs.readFileSync(path.join(source,name),'utf8');}
function method(text,prefix){var at=text.indexOf(prefix);if(at<0||text.indexOf(prefix,at+1)>=0)throw Error('missing/ambiguous seam '+prefix);var brace=text.indexOf('{',at),level=0;for(var i=brace;i<text.length;i++){if(text[i]==='{')level++;if(text[i]==='}'&&--level===0)return text.slice(at,i+1);}throw Error('unclosed seam '+prefix);}
// Brace extraction is only used on these known methods; assert generated translation unit through compilation.
var tags=read('ScriptTags.cpp'),wrap=read('PapyrusWrap.cpp');
fs.mkdirSync(out,{recursive:true});
for(var name of ['ScriptTags.cpp','ScriptTags.h','PapyrusWrap.cpp']) {var bytes=fs.readFileSync(path.join(source,name));fs.writeFileSync(path.join(out,name+'.source'),bytes);console.log(name+' SHA256 '+crypto.createHash('sha256').update(bytes).digest('hex'));}
fs.writeFileSync(path.join(out,'ScriptTags.h'),read('ScriptTags.h'));
fs.writeFileSync(path.join(out,'Common.h'),'#pragma once\n#include "FakeEngine.h"\n');
var text='#include "FakeEngine.h"\n#include "ScriptTags.h"\nnamespace BlackFlame { namespace {\n';
var start=tags.indexOf('        using Clock ='),end=tags.indexOf('        void Add(');
if(start<0||end<start)throw Error('tag state seam missing');text+=tags.slice(start,end);
for(var sig of ['void Add(', 'bool JudgePending('])text+='\n'+method(tags,sig)+'\n';
text+='}\n';
for(var sig of ['void TagPlaced(', 'void TagSource(', 'void TagPending(', 'TagInfo TagInfoOf(', 'TagState TagStateOf(', 'bool IsTaggedSource(', 'bool IsTaggedVisualInstance(', 'bool ResolvePending(', 'void ResetScriptTags('])text+='\n'+method(tags,sig)+'\n';
text+='}\nnamespace Wrapper { using namespace BlackFlame;\n';
text+=method(wrap,'enum class Wrapped')+';\n'+method(wrap,'struct Decision')+';\n';
text+='inline std::atomic<int> g_deferLogs{0};\n';
for(var sig of ['RE::BSScript::Internal::VirtualMachine* VM(', 'bool IsActiveEffectHandle(', 'bool CallerIsPlayersFireEffect(', 'void Tag(', 'void StorePending(', 'void Decide(', 'RE::ObjectRefHandle HandleOf('])text+='\n'+method(wrap,sig)+'\n';
text+='struct FakeNative { bool latent=false;bool GetIsLatent()const{return latent;} };\nstruct Proxy {FakeNative* m_native;std::string GetName()const{return "PlaceAtMe";}\n'+method(wrap,'void After(')+'\n};\n}\n';
fs.writeFileSync(path.join(out,'Seam.cpp'),text);
