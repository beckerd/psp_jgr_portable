-- Observe the original title, without supplied movement, progress or saves.
-- Camera callback wrapper copies completed actor/camera/controller state.
local out=assert(os.getenv('JGR_REFERENCE_OUT'))
local m=flycast.memory
local frame,count,last,camera,player=0,0,0,0,0
local code,serial,dest=0x8cfe4000,0x8cfe5000,0x8cfe6000
local file=assert(io.open(out..'title_actor_camera.bin','wb'))
file:write('TAC2',string.pack('<I4I4I4',0x900,0x290,0xd0))
local log=assert(io.open(out..'title.csv','w'))
log:write('vblank,sample,main_state,character,player_clock,x,y,z,state,motion,cam_mode,song,adx\n')
local function valid(p)return p>=0x8c000000 and p<0x8d000000 end
local function rf(a)return (string.unpack('<f',string.pack('<I4',m.read32(a))))end
local function find(id,exec)
 local n=0
 local function walk(t)
  while valid(t) and n<2000 do
   n=n+1
   if (id and (m.read32(t+4)&65535)==id) or (exec and m.read32(t+8)==exec)then return t end
   local q=walk(m.read32(t+0x1c));if q~=0 then return q end;t=m.read32(t+0x24)
  end;return 0
 end;return walk(m.read32(0x8c2e9c9c))
end
local function bytes(p,n)
 local a={};for j=0,n-1,4 do a[#a+1]=string.pack('<I4',m.read32(p+j))end;return table.concat(a)
end
local function install(c,p)
 local words,loads={},{}
 local function op(v)words[#words+1]=v end
 local function ld(r,v)loads[#loads+1]={#words,r,v};op(0)end
 local function copy(from,to,n)
  ld(1,from);ld(2,to);ld(3,n//4)
  op(0x6016);op(0x2202);op(0x7204);op(0x4310);op(0x8bfa)
 end
 op(0x4f22);ld(4,c);ld(0,0x8c01dc40);op(0x400b);op(9)
 copy(p,dest,0x900);copy(c,dest+0x900,0x290)
 copy(m.read32(p+0x18),dest+0xb90,0xd0)
 -- Runtime local pose: original 20-node CNK, including blend results.
 ld(1,p+0x594);op(0x6112);ld(2,dest+0x1000);ld(3,0x100)
 op(0x6016);op(0x2202);op(0x7204);op(0x4310);op(0x8bfa)
 ld(1,serial);op(0x6012);op(0x7001);op(0x2102)
 op(0x4f26);op(0xb);op(9);if #words%2~=0 then op(9)end
 local pool=code+#words*2
 for j,l in ipairs(loads)do local pc=code+l[1]*2;local a=pool+(j-1)*4
  words[l[1]+1]=0xd000+l[2]*256+(a-((pc+4)&0xfffffffc))//4;m.write32(a,l[3])end
 for j,w in ipairs(words)do m.write16(code+(j-1)*2,w)end
 m.write32(serial,0);last=0;camera=c;player=p;m.write32(c+8,code)
 print(string.format('TITLE_REFERENCE installed camera=%08x player=%08x',c,p));io.stdout:flush()
end
flycast_callbacks={vblank=function()
 frame=frame+1
 local main=find(6);local stage=find(0x1b)
 if stage~=0 then
  local c=find(nil,0x8c01dc40)
  if c~=0 then local p=m.read32(c+0x18)
   if valid(p) and m.read32(p+8)==0x8c0198f8 and c~=camera then install(c,p)end
  end
 end
 if camera~=0 then
  local s=m.read32(serial)
  if s~=last then
   assert(s==last+1,'missed completed callback');last=s;count=count+1
   file:write(bytes(dest,0xc60),bytes(dest+0x1000,0x400));file:flush()
   local bgm=find(0x298)
   log:write(string.format('%d,%d,%u,%u,%u,%.9g,%.9g,%.9g,%u,%u,%u,%u,%u\n',frame,count,
    main~=0 and m.read32(main+0xa8)or 0,main~=0 and m.read32(main+0x78)or 0,
    m.read32(dest+0x98),rf(dest+0xb8),rf(dest+0xbc),rf(dest+0xc0),m.read32(dest+0x70),
    m.read32(dest+0x58c),m.read32(dest+0x970),bgm~=0 and m.read32(bgm+0x88)or 0,
    bgm~=0 and m.read32(bgm+0x7c)or 0));log:flush()
   if os.getenv('JGR_REFERENCE_FRAMEBUFFER')=='1' and (count==60 or count==120 or count==240 or count==480)then
    local address=0xa5000000+(m.read32(0xa05f8050)&0x7fffff)
    local f=assert(io.open(out..'title_'..count..'.raw','wb'));f:write(bytes(address,640*480*2));f:close()
   end
  end
  if stage==0 or count>=5600 then file:close();log:close();print('TITLE_REFERENCE complete samples='..count);os.exit(0)end
 end
 if frame%1200==0 then print('TITLE_REFERENCE vblank='..frame..' main='..string.format('%08x',main~=0 and m.read32(main+0xa8)or 0));io.stdout:flush()end
 if frame>60000 then error('title capture timed out')end
end}
