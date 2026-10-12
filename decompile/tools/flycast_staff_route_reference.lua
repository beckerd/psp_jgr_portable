-- Complete original STAFF performances, in the existing TAC2 demo format.
-- Supplied ending/selected-character entry only; no input, trajectory, pose,
-- graffiti-progress, VMU or savestate writes. Run once per selected skater.
-- The global camera entry hook catches creation-pass callbacks as well as
-- ordinary and spray camera modes; every original instruction still runs.
local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')
local out=assert(os.getenv('JGR_REFERENCE_OUT'))
local selected=tonumber(os.getenv('JGR_STAFF_CHARACTER') or '0')
assert(selected>=0 and selected<16 and selected%1==0)
local limit=tonumber(os.getenv('JGR_STAFF_ROUTE_SAMPLES') or '0')
dofile(root..'tools/flycast_ending_reference.lua')
local base=flycast_callbacks.vblank
local m=flycast.memory
local entry,code,trampoline,serial,dest,staff_slot=0x8c01dc40,0x8cfe4000,0x8cfe4400,0x8cfe5000,0x8cfe6000,0x8cfe5100
local file=assert(io.open(out..'title_actor_camera.bin','wb'))
file:write('TAC2',string.pack('<I4I4I4',0x900,0x290,0xd0))
local log=assert(io.open(out..'title.csv','w'))
log:write('vblank,sample,main_state,character,player_clock,x,y,z,state,motion,cam_mode,song,adx,staff_row,staff_clock,staff_pointer,task19_pointer,task19_phase,actor_pointer,actor_character,actor_variant,controller_pointer,controller_record,controller_repeat,controller_input,gf_small,gf_large,part,selected_character,player_tick,replay_tick,staff_row_pointer\n')
local spray=assert(io.open(out..'staff_spray.csv','w'))
spray:write('sample,frame,active,variant,ref,pose_frame,target_x,target_y,target_z\n')
local graffiti=assert(io.open(out..'staff_graffiti.csv','w'))
graffiti:write('sample,frame,area,index,stroke,reveal,complete,started,revealing,descriptor,pointer,busy\n')
local prior_spray,prior_graffiti,descriptor_ids='',{},{}
local gf=assert(io.open(out..'staff_graffiti.bin','wb'))
-- SGF1 record: two atomic GF images (actor+268, actor+274), then atomic
-- staff-task work. Their pointers live in the associated TAC2 actor/CSV.
gf:write('SGF1',string.pack('<I4I4I4',0x158,2,0xd0))
local summary=assert(io.open(out..'staff_route_metadata.json','w'))
local frame,count,last,installed,started,parts=0,0,0,false,false,0
local prior_actor,prior_tick,prior_row=0,nil,nil
local first_tick,last_tick,part_counts={},{},{}
local finish=os.exit
local closed=false
local function close(status,reason)
 if closed then return end;closed=true
 file:close();log:close();gf:close();spray:close();graffiti:close()
 local records={}
 for i=1,parts do records[#records+1]=string.format('{"part":%d,"first_tick":%d,"last_tick":%d,"samples":%d}',i-1,first_tick[i],last_tick[i],part_counts[i])end
 summary:write(string.format('{"character":%d,"samples":%d,"parts":%d,"complete":%s,"reason":"%s","streams":[%s]}\n',selected,count,parts,status==0 and reason=='staff_deleted' and 'true' or 'false',reason,table.concat(records,',')))
 summary:close();print(string.format('STAFF_ROUTE complete character=%d samples=%d parts=%d reason=%s',selected,count,parts,reason));io.stdout:flush()
end
os.exit=function(status)close(status or 0,'parent_exit');return finish(status)end
local function valid(p,n)return p>=0x8c000000 and p+(n or 4)<=0x8d000000 and p%4==0 end
local function bytes(p,n)
 assert(valid(p,n));local a={};local words=m.readTable32(p,n//4)
 for j=0,n-1,4 do local w=words[p+j];assert(w~=nil,'incomplete memory table');a[#a+1]=string.pack('<I4',w)end
 return table.concat(a)
end
local function real(p)return (string.unpack('<f',string.pack('<I4',m.read32(p))))end
local function tasks()
 local found,n={},0;found.graffiti={};found.revealing={}
 local function walk(t)
  while valid(t,0x28) and n<2000 do
   n=n+1;local id=m.read32(t+4)&65535
   if not found[id] then found[id]=t end
   local exec=m.read32(t+8)
   if exec==0x8c03e6f8 then local p=m.read32(t+0x6c);if valid(p,0x158)then found.graffiti[p]=true end end
   if exec==0x8c03b828 or exec==0x8c03b8a8 then found.revealing[m.read32(t+0x6c)]=exec==0x8c03b828 and 1 or 2 end
   walk(m.read32(t+0x1c));t=m.read32(t+0x24)
  end
 end
 walk(m.read32(0x8c2e9c9c));assert(n<2000,'task graph overflow');return found
end
local function emit(address,build)
 local words,loads,branches,labels={},{},{},{}
 local function op(v)words[#words+1]=v end
 local function ld(r,v)loads[#loads+1]={#words,r,v};op(0)end
 local function label(name)assert(not labels[name]);labels[name]=#words end
 local function branch(kind,name)branches[#branches+1]={#words,kind,name};op(0);if kind==0xa000 then op(9)end end
 build(op,ld,label,branch)
 if #words%2~=0 then op(9)end
 local pool=address+#words*2
 for j,l in ipairs(loads)do local pc=address+l[1]*2;local a=pool+(j-1)*4;local delta=(a-((pc+4)&0xfffffffc))//4
  assert(delta>=0 and delta<=255,'literal pool out of range')
  words[l[1]+1]=0xd000+l[2]*256+delta;m.write32(a,l[3])
 end
 for _,b in ipairs(branches)do local delta=assert(labels[b[3]])-b[1]-2
  if b[2]==0xa000 then assert(delta>=-2048 and delta<=2047);words[b[1]+1]=b[2]+(delta&4095)
  else assert(delta>=-128 and delta<=127);words[b[1]+1]=b[2]+(delta&255)end
 end
 for j,v in ipairs(words)do m.write16(address+(j-1)*2,v)end
 return pool+#loads*4
end
local function install()
 -- The known six-instruction prologue contains no PC-relative operands.
 -- Obtain its bytes only from the user's running executable.
 local original,signature={},2166136261
 for j=0,5 do
  local w=m.read16(entry+j*2);original[#original+1]=w
  signature=((signature~(w&255))*16777619)&0xffffffff
  signature=((signature~(w>>8))*16777619)&0xffffffff
 end
 assert(signature==0xc7cbfcda,'unexpected camera executable')
 emit(trampoline,function(op,ld)
  for _,v in ipairs(original)do op(v)end
  ld(0,entry+12);op(0x402b);op(9)
 end)
 local stop=emit(code,function(op,ld,label,branch)
  local function copy(n,to) -- r1=source
   ld(2,to);ld(3,n//4);op(0x6016);op(0x2202);op(0x7204);op(0x4310);op(0x8bfa)
  end
  local function fixed(from,to,n)ld(1,from);copy(n,to)end
  local function optional(slot,to,n,name)
   ld(1,slot);op(0x6112)
   ld(2,0x8c000000);op(0x3122);branch(0x8b00,name..'_zero')
   ld(2,0x8d000000-n);op(0x3212);branch(0x8b00,name..'_zero')
   copy(n,to);branch(0xa000,name..'_done');label(name..'_zero')
   ld(2,to);ld(3,n//4);op(0xe000);op(0x2202);op(0x7204);op(0x4310);op(0x8bfb)
   label(name..'_done')
  end
  op(0x4f22);op(0x2f86);op(0x6843) -- PR/r8 preserved; r8=camera
  ld(0,trampoline);op(0x400b);op(9)
  -- A nonzero authored STAFF camera-list pointer scopes the observation.
  op(0x6183);ld(2,0x274);op(0x312c);op(0x6012);op(0x2008);branch(0x8b00,'capture')
  branch(0xa000,'done');label('capture')
  op(0x6183);op(0x5016);op(0x6103);copy(0x900,dest)
  op(0x6183);copy(0x290,dest+0x900)
  optional(dest+0x18,dest+0xb90,0xd0,'parent')
  optional(dest+0x594,dest+0x1000,0x400,'pose')
  optional(dest+0x268,dest+0x1400,0x158,'small')
  optional(dest+0x274,dest+0x1600,0x158,'large')
  optional(staff_slot,dest+0x1800,0xd0,'staff')
  ld(1,dest+0x1a00);op(0x2182) -- original camera pointer
  op(0x6183);op(0x5016);ld(1,dest+0x1a04);op(0x2102) -- actor pointer
  ld(1,serial);op(0x6012);op(0x7001);op(0x2102)
  label('done');op(0x68f6);op(0x4f26);op(0xb);op(9)
 end)
 assert(stop<trampoline,'camera wrapper overlaps trampoline')
 m.write32(serial,0);m.write32(staff_slot,0)
 m.write16(entry,0xd001);m.write16(entry+2,0x402b);m.write16(entry+4,9);m.write16(entry+6,9);m.write32(entry+8,code)
 installed=true;print('STAFF_ROUTE global completed-camera hook installed');io.stdout:flush()
end
local function sample(found)
 local current=m.read32(serial)
 if current==last then return end
 assert(current==last+1,'missed completed camera callback');last=current;count=count+1
 local actor=m.read32(dest+0x1a04);local tick=m.read32(dest+0x98)
 local row=m.read32(dest+0x1800+0x6c);local clock=m.read32(dest+0x1800+0x80)
 assert(valid(m.read32(dest+0x594),0x400),'missing local pose')
 if actor~=prior_actor or (prior_tick and tick<prior_tick) or row~=prior_row then
  parts=parts+1;assert(parts<=9,'too many STAFF streams');first_tick[parts]=tick;part_counts[parts]=0
  print(string.format('STAFF_ROUTE stream=%d row=%08x first_tick=%d',parts,row,tick));io.stdout:flush()
 else assert(tick==prior_tick+1,'missing or duplicated actor tick')end
 prior_actor,prior_tick,prior_row=actor,tick,row;last_tick[parts]=tick;part_counts[parts]=part_counts[parts]+1
 file:write(bytes(dest,0xc60),bytes(dest+0x1000,0x400))
 gf:write(bytes(dest+0x1400,0x158),bytes(dest+0x1600,0x158),bytes(dest+0x1800,0xd0))
 local main=assert(found[6]);assert(m.read32(main+0x78)==selected,'selected character changed')
 local controller=m.read32(dest+0x18);local parent=found[0x19]or 0;local bgm=found[0x298]or 0
 local values={frame,count,m.read32(main+0xa8),selected,tick,real(dest+0xb8),real(dest+0xbc),real(dest+0xc0),
  m.read32(dest+0x70),m.read32(dest+0x58c),m.read32(dest+0x970),bgm~=0 and m.read32(bgm+0x88)or 0,bgm~=0 and m.read32(bgm+0x7c)or 0,
  (row-0x8cfe0000)//20,clock,m.read32(staff_slot),parent,parent~=0 and m.read32(parent+0x6c)or 0,actor,m.read32(dest+0x7c),
  m.read32(dest+0x560)==m.read32(dest+0x580) and 1 or 0,controller,
  m.read32(dest+0xb90+0x70),m.read32(dest+0xb90+0x78),
  m.read32(dest+0xb90+0x6c),m.read32(dest+0x268),m.read32(dest+0x274),parts-1,selected,tick,m.read32(dest+0x978),row}
 for j,v in ipairs(values)do values[j]=string.format('%.17g',v)end
 log:write(table.concat(values,',')..'\n')
 local cue={m.read32(dest+0x294),m.read32(dest+0x560)==m.read32(dest+0x580)and 1 or 0,m.read32(dest+0x2b8),real(dest+0x29c),real(dest+0x2a0),real(dest+0x2a4),real(dest+0x2a8)}
 for j,v in ipairs(cue)do cue[j]=string.format('%.17g',v)end
 local cue_line=table.concat(cue,',')
 if cue_line~=prior_spray or part_counts[parts]==1 then spray:write(count..','..(count-1)..','..cue_line..'\n');prior_spray=cue_line end
 -- Complete live wall state at this vblank, including reveal tasks after
 -- the actor releases its target. Sparse by original descriptor identity.
 if part_counts[parts]==1 then
  prior_graffiti={};descriptor_ids={}
  for area=0,5 do
   local table=m.read32(0x8cddf04c+area*4);local index=0
   while valid(table,4) and m.read32(table)~=0 and index<512 do
    descriptor_ids[m.read32(table)]={area,index};table=table+4;index=index+1
   end
  end
 end
 for p in pairs(found.graffiti)do
  local desc=m.read32(p+0xb8);local id=descriptor_ids[desc]
  if id then
   local area,index=id[1],id[2]
   local state={area,index,m.read32(p+0x144),m.read32(p+0x148),m.read32(p+0x14c),m.read32(p+0x150),found.revealing[p]or 0,desc,p,m.read32(p+0x154)}
   local line=table.concat(state,',');local key=area..':'..index
   if prior_graffiti[key]~=line then graffiti:write(count..','..(count-1)..','..line..'\n');prior_graffiti[key]=line end
  end
 end
 if count%120==0 then file:flush();gf:flush();log:flush()end
 if limit>0 and count>=limit then close(0,'sample_limit');finish(0)end
end
flycast_callbacks.vblank=function()
 -- Patch before the original parent's first update, not after task9ff
 -- becomes visible: that first update already chooses movie25 vs STAFF.
 m.write16(0x8c0ee90c,0xe502)
 base();frame=frame+1
 local found=tasks();local ending=found[0x9ff]
 if ending and not installed then
  local main=assert(found[6]);m.write32(main+0x78,selected)
  -- Same supplied staff-only entry used by the established camera capture.
  -- Original stage/skater/card loaders and STAFF update tasks remain live.
  m.write16(0x8c0ee90c,0xe502);install()
 end
 if installed then sample(found)end
 local staff=found[0x1b]
 if staff then started=true;m.write32(staff_slot,staff)
 elseif started then
  assert(parts==9 and count>8000,'incomplete STAFF performance')
  close(0,'staff_deleted');finish(0)
 end
 if frame>60000 then close(1,'timeout');error('STAFF route capture timed out')end
end
