-- Development-only oracle: stop immediately around one unchanged original
-- player callback. These RAM images are private test fixtures, never installer
-- inputs. The fresh-disc runner disables all savestate access.
local m=flycast.memory
local phase,meta,timers,count=0x8cfe3000,0x8cfe2400,0x8cfe3100,0x8cfe30f0
local wrapper,timer_code=0x8cfe2000,0x8cfe2200
local actor,observed,frame=0,0,0
local wanted_tick=tonumber(os.getenv('JGR_REPLAY_TICK') or '64')
local base=assert(flycast_callbacks.vblank)
local out=assert(os.getenv('JGR_REFERENCE_OUT'))
local function valid(p)return p>=0x8c000000 and p<0x8d000000 end
local function dump(name,p,n)
 local f=assert(io.open(out..name,'wb'))
 for a=p,p+n-1,16384 do
  local size=math.min(16384,p+n-a);local words=m.readTable32(a,size//4);local pieces={}
  for b=a,a+size-1,4 do pieces[#pieces+1]=string.pack('<I4',assert(words[b]))end
  f:write(table.concat(pieces))
 end
 f:close()
end
local function assembler(address)
 local words,loads={},{}
 local function op(w)words[#words+1]=w end
 local function ld(r,value)loads[#loads+1]={#words,r,value};op(0)end
 local function wait(value)
  local start=#words;op(0x6012);op(0x8800+value)
  local index=#words;op(0x8900+((start-index-2)&255))
 end
 local function finish()
  if #words%2~=0 then op(9)end
  local pool=address+#words*2
  for i,row in ipairs(loads)do
   local pc=address+row[1]*2;local target=pool+(i-1)*4
   words[row[1]+1]=0xd000+row[2]*256+(target-((pc+4)&0xfffffffc))//4
   m.write32(target,row[3])
  end
  for i,w in ipairs(words)do m.write16(address+(i-1)*2,w)end
 end
 return op,ld,wait,finish
end
local function install(p)
 actor=p
 local op,ld,wait,finish=assembler(wrapper)
 op(0x4f22);op(0x2f46);ld(1,phase);op(0xe001);op(0x2102)
 op(0x006a);ld(2,meta);op(0x2202);op(0x60f3);op(0x1201)
 op(0x0002);op(0x1202);op(0x005a);op(0x1203)
 ld(2,meta+64)
 for bank=1,2 do
  for register=0,15 do op(0xf20a+register*16);op(0x7204)end
  op(0xfbfd)
 end
 wait(1)
 op(0x64f2);ld(0,0x8c0198f8);op(0x400b);op(9)
 ld(1,phase);op(0xe002);op(0x2102);wait(2)
 op(0x64f6);op(0x4f26);op(0xb);op(9);finish()
 -- Preserve the original timer helper's result and clobbered registers,
 -- while logging actual device reads for exact private callback replay.
 op,ld,wait,finish=assembler(timer_code)
 ld(2,0xffd8000c);op(0x6322)
 ld(2,count);op(0x6022);op(0x7001);op(0x2202);op(0x70ff);op(0x4008)
 ld(2,timers);op(0x320c);op(0x2232);op(0xe0ff);op(0xb);op(0x3038);finish()
 local changed=0
 for a=0x8c010000,0x8c2feffc,4 do
  if m.read32(a)==0x8c28a9b6 then m.write32(a,timer_code);changed=changed+1 end
 end
 m.write32(count,0);m.write32(phase,0);m.write32(p+8,wrapper)
 print(string.format('REPLAY_VM installed actor=%08x timer_refs=%d',p,changed));io.stdout:flush()
end
local function find_demo()
 local visits=0
 local function walk(t)
  while valid(t) and visits<2000 do
   visits=visits+1
   if m.read32(t+8)==0x8c0198f8 and m.read32(t+0x7c)==1 and m.read32(t+0x98)>=wanted_tick then
    local c=m.read32(t+0x18)
    if valid(c) and m.read32(c+0x84)==4 then return t end
   end
   local p=walk(m.read32(t+0x1c));if p~=0 then return p end;t=m.read32(t+0x24)
  end
  return 0
 end
 return walk(m.read32(0x8c2e9c9c))
end
flycast_callbacks.vblank=function()
 frame=frame+1;base()
 if actor==0 and JGR_REFERENCE_READY then local p=find_demo();if p~=0 then install(p)end end
 if actor~=0 then
  local state=m.read32(phase)
  if state==1 and observed==0 then
   m.write32(count,0);dump('before.ram',0x8c000000,0x1000000)
   observed=1;m.write32(phase,3)
  elseif state==2 and observed==1 then
   local n=m.read32(count);assert(n<=64,'timer oracle overflow')
   dump('after.ram',0x8c000000,0x1000000);dump('timer_reads.bin',timers,n*4)
   local f=assert(io.open(out..'callback.json','w'))
   f:write(string.format('{"actor":%u,"entry":%u,"fpscr":%u,"stack":%u,"sr":%u,"fpul":%u,"fpu_image":%u,"timer_reads":%u,"private_fixture_only":true}\n',actor,0x8c0198f8,m.read32(meta),m.read32(meta+4),m.read32(meta+8),m.read32(meta+12),meta+64,n));f:close()
   print('REPLAY_VM_CALLBACK complete');io.stdout:flush();os.exit(0)
  end
 end
 if frame>12000 then error('callback oracle timed out')end
end
