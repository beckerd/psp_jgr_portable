-- Fresh-disc title extraction. Observe complete original camera callbacks,
-- including creation ticks; choose the baseline skater before its bank loads.
local out=assert(os.getenv('JGR_REFERENCE_OUT'))
local m=flycast.memory
local entry,code,trampoline,serial,dest=0x8c01dc40,0x8cfe4000,0x8cfe4400,0x8cfe5000,0x8cfe6000
local frame,count,last,installed,started,done=0,0,0,false,false,false
local file=assert(io.open(out..'title_actor_camera.bin','wb'))
file:write('TAC2',string.pack('<I4I4I4',0x900,0x290,0xd0))
local log=assert(io.open(out..'title.csv','w'))
log:write('vblank,sample,main_state,character,player_clock,x,y,z,state,motion,cam_mode,song,adx\n')
local function valid(p)return p>=0x8c000000 and p<0x8d000000 and p%4==0 end
local function task(id)
 local n=0
 local function walk(t)
  while valid(t) and n<2000 do
   n=n+1;if(m.read32(t+4)&65535)==id then return t end
   local p=walk(m.read32(t+0x1c));if p~=0 then return p end;t=m.read32(t+0x24)
  end;return 0
 end
 return walk(m.read32(0x8c2e9c9c))
end
local function bytes(p,n)
 local a={};local words=m.readTable32(p,n//4)
 for j=0,n-1,4 do a[#a+1]=string.pack('<I4',assert(words[p+j]))end
 return table.concat(a)
end
local function real(p)return(string.unpack('<f',string.pack('<I4',m.read32(p))))end
local function emit(address,build)
 local words,loads={},{}
 local function op(v)words[#words+1]=v end
 local function ld(r,v)loads[#loads+1]={#words,r,v};op(0)end
 build(op,ld);if #words%2~=0 then op(9)end
 local pool=address+#words*2
 for j,l in ipairs(loads)do
  local pc=address+l[1]*2;local a=pool+(j-1)*4;local delta=(a-((pc+4)&0xfffffffc))//4
  assert(delta>=0 and delta<256);words[l[1]+1]=0xd000+l[2]*256+delta;m.write32(a,l[3])
 end
 for j,v in ipairs(words)do m.write16(address+(j-1)*2,v)end
 return pool+#loads*4
end
local function install()
 -- Copy the displaced prologue from the user's running executable. The
 -- fingerprint validates its known non-PC-relative instructions without
 -- distributing the original instruction bytes in this capture tool.
 local original,signature={},2166136261
 for j=0,5 do
  local w=m.read16(entry+j*2);original[#original+1]=w
  signature=((signature~(w&255))*16777619)&0xffffffff
  signature=((signature~(w>>8))*16777619)&0xffffffff
 end
 assert(signature==0xc7cbfcda,'unsupported camera entry')
 emit(trampoline,function(op,ld)
  for _,v in ipairs(original)do op(v)end
  ld(0,entry+12);op(0x402b);op(9)
 end)
 local stop=emit(code,function(op,ld)
  local function copy(n,to)
   ld(2,to);ld(3,n//4);op(0x6016);op(0x2202);op(0x7204);op(0x4310);op(0x8bfa)
  end
  op(0x4f22);op(0x2f86);op(0x6843)
  ld(0,trampoline);op(0x400b);op(9)
  op(0x6183);op(0x5016);op(0x6103);copy(0x900,dest)
  op(0x6183);copy(0x290,dest+0x900)
  ld(1,dest+0x18);op(0x6112);copy(0xd0,dest+0xb90)
  ld(1,dest+0x594);op(0x6112);copy(0x400,dest+0x1000)
  ld(1,serial);op(0x6012);op(0x7001);op(0x2102)
  op(0x68f6);op(0x4f26);op(0xb);op(9)
 end)
 assert(stop<trampoline);m.write32(serial,0)
 m.write16(entry,0xd001);m.write16(entry+2,0x402b);m.write16(entry+4,9);m.write16(entry+6,9);m.write32(entry+8,code)
 installed=true
end
flycast_callbacks={vblank=function()
 if done then return end
 frame=frame+1
 local main=task(6);local stage=task(0x1b)
 if main~=0 then
  local state=m.read32(main+0xa8)
  if state>=0x8c015b7c and state<=0x8c015dd4 then
   m.write32(main+0x78,7)
   if not installed then install()end
  end
 end
 if installed then
  local current=m.read32(serial)
  if current~=last then
   assert(current==last+1,'missed completed title callback');last=current;count=count+1;started=true
   local bgm=task(0x298)
   file:write(bytes(dest,0xc60),bytes(dest+0x1000,0x400))
   log:write(string.format('%d,%d,%u,7,%u,%.9g,%.9g,%.9g,%u,%u,%u,%u,%u\n',
    frame,count,main~=0 and m.read32(main+0xa8)or 0,m.read32(dest+0x98),
    real(dest+0xb8),real(dest+0xbc),real(dest+0xc0),m.read32(dest+0x70),m.read32(dest+0x58c),
    m.read32(dest+0x970),bgm~=0 and m.read32(bgm+0x88)or 0,bgm~=0 and m.read32(bgm+0x7c)or 0))
   if count%120==0 then file:flush();log:flush()end
  end
  if started and stage==0 then
   file:close();log:close();done=true
   print('TITLE_REFERENCE complete samples='..count);io.stdout:flush();os.exit(0)
  end
 end
 if frame%1200==0 then print('PORTABLE_TITLE vblank='..frame..' samples='..count);io.stdout:flush()end
 if frame>60000 then error('title capture timed out')end
end}
