-- Observe sound task initializers while the unchanged original demo runs.
-- Tail-call each original initializer with its untouched r4/r5 arguments.
local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')
dofile(root..'tools/flycast_recruit_demo_reference.lua');local base=flycast_callbacks.vblank
local m=flycast.memory;local installed=false;local last=0
local serial,ring=0x8cfe9000,0x8cfea000
local log=assert(io.open(os.getenv('JGR_REFERENCE_OUT')..'audio.csv','w'))
log:write('serial,tick,kind,bank,program,parent,task\n');log:flush()
local function install()
 for i,entry in ipairs({{0x8c05d288,2},{0x8c05d2c0,1},{0x8c05d2f8,3},{0x8c05d330,4}})do
  local code=0x8cfe8000+(i-1)*0x100;local words,loads={},{}
  local function op(v)words[#words+1]=v end
  local function ld(r,v)loads[#loads+1]={#words,r,v};op(0)end
  local function put()op(0x2302);op(0x7304)end
  ld(1,serial);op(0x6012);op(0x6203);op(0x7001);op(0x2102)
  op(0x4208);op(0x4208);op(0x4200);ld(3,ring);op(0x332c)
  op(0x5146);op(0x6013);op(0x707f);op(0x7019);op(0x6002);put()
  op(0xe000+entry[2]);put();op(0x6053);put();op(0x6013);put();op(0x6043);put()
  ld(0,entry[1]);op(0x402b);op(9)
  if #words%2~=0 then op(9)end
  local pool=code+#words*2
  for j,l in ipairs(loads)do local pc=code+l[1]*2;local a=pool+(j-1)*4
   words[l[1]+1]=0xd000+l[2]*256+(a-((pc+4)&0xfffffffc))//4;m.write32(a,l[3])end
  for j,w in ipairs(words)do m.write16(code+(j-1)*2,w)end
  local refs=0
  for a=0x8c010000,0x8c170000-4,4 do
   if m.read32(a)==entry[1]then m.write32(a,code);refs=refs+1 end
  end
  assert(refs>0);print(string.format('AUDIO_REFERENCE hook=%08x refs=%d',entry[1],refs))
 end
 m.write32(serial,0);installed=true
end
flycast_callbacks.vblank=function()
 base()
 if JGR_REFERENCE_READY and not installed then install()end
 if installed then
  local count=m.read32(serial);assert(count<700,'audio capture overflow')
  while last<count do
   local a=ring+last*32;local value=m.read32(a+8)
   log:write(string.format('%d,%d,%d,%d,%d,%u,%u\n',last,m.read32(a),m.read32(a+4),value>>16,value&65535,m.read32(a+12),m.read32(a+16)))
   last=last+1
  end
  log:flush()
 end
end
