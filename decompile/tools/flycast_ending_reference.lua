-- Enter the ORIGINAL ending parent after read-only state load and task
-- cleanup. This is a supplied ending context, not an earned final clear.
-- No saveState/saveVMU calls. Choose No at the ending SAVE? prompt.
local outdir=assert(os.getenv('JGR_REFERENCE_OUT'))
local m,i=flycast.memory,flycast.input
local frame,cleanup,started,last,choice=0,0,false,'',0
local scenario=tonumber(os.getenv('JGR_REFERENCE_SCENE'))or 25
local flow_only=scenario==125 or scenario==126 or scenario==225
local draw_callbacks={}
local log=assert(io.open(outdir..'ending.csv','w'))
log:write('vblank,parent,sub,movie,movie_phase,segment,staff_phase,staff_clock,staff_pc,card_phase,card_kind,card_hold,card_buffer,card_page,card_offset,card_step,card_target,card_pattern\n')
local function rf(a)return (string.unpack('<f',string.pack('<I4',m.read32(a))))end
local function si(x)return x>=0x80000000 and x-0x100000000 or x end
local function task(id)
 local n=0
 local function walk(t)
  while t>=0x8c000000 and t<0x8d000000 and n<2000 do
   n=n+1;if(m.read32(t+4)&0xffff)==id then return t end
   local p=walk(m.read32(t+0x1c));if p~=0 then return p end;t=m.read32(t+0x24)
  end;return 0
 end;return walk(m.read32(0x8c2e9c9c))
end
local function shot(name)
 if os.getenv('JGR_REFERENCE_FRAMEBUFFER')~='1'then return end
 local base=0xa5000000+(m.read32(0xa05f8050)&0x7fffff)
 local words=m.readTable32(base,640*480//2);local f=assert(io.open(outdir..name..'.raw','wb'));local b={}
 for k=0,640*480//2-1 do b[#b+1]=string.pack('<I4',words[base+k*4]or 0)
  if #b==4096 then f:write(table.concat(b));b={}end end
 f:write(table.concat(b));f:close()
end
flycast_callbacks={vblank=function()
 frame=frame+1;i.releaseButtons(1,0xffffffff);i.setAxis(1,1,0);i.setAxis(1,2,0)
 if frame==240 then flycast.emulator.loadState(1)end
 if frame==360 then
  local main=task(6);assert(main~=0);local child=m.read32(main+0x1c)
  while child~=0 do m.write32(child,m.read32(child)|1);child=m.read32(child+0x24)end
  m.write32(0x8cfe8000,0x0009000b);m.write32(main+0xa8,0x8cfe8000);cleanup=main
 end
 if cleanup~=0 and m.read32(cleanup+0x1c)==0 then
  -- Match the outgoing level's resource release BEFORE DEMO overwrites
  -- its old RAM banks; simply deleting tasks leaves stale texlist cells.
  local start=0x8cfe8020
  local code={0x4f22,0x2fe6,0x6e43,0xe404,0xd006,0x400b,9,0xd006,0x400b,9,
   0x64e3,0xd005,0x400b,9,0x6ef6,0x4f26,0xb,9,
   0x70a0,0x8c01,0x91bc,0x8c0d,0x6314,0x8c01}
  for k,w in ipairs(code)do m.write16(start+(k-1)*2,w)end
  m.write32(cleanup+0xa8,start);cleanup=0
 end
 local e=task(0x9ff);local movie=task(0x8be);local staff=task(0x1b)
 -- Optional FLOW-only trace: stage rendering makes the interpreter miss
 -- many VBlanks. Disable draw callbacks while staff task 1b exists, leaving
 -- all original update/input/audio/loader/card callbacks running. This
 -- variant cannot establish visual parity or natural render cadence.
 if flow_only then
  local function visit(t)
   while t>=0x8c000000 and t<0x8d000000 do
    -- 0b34a4/0b3558 store draw at +14h. +0ch is initializer.
    local draw=m.read32(t+20);local saved=draw_callbacks[t]
    if staff~=0 then
     if draw~=0 then draw_callbacks[t]={draw=draw,id=m.read32(t+4),update=m.read32(t+8)};m.write32(t+20,0)end
    elseif saved and draw==0 and saved.id==m.read32(t+4)and saved.update==m.read32(t+8)then
     m.write32(t+20,saved.draw);draw_callbacks[t]=nil
    end
    visit(m.read32(t+0x1c));t=m.read32(t+0x24)
   end
  end;visit(m.read32(0x8c2e9c9c))
 end
 if e~=0 then
  started=true;local p,sub=m.read32(e+0x6c),m.read32(e+0x70)
  local key=p..':'..sub
  if key~=last then print('ENDING phase '..key..' vblank='..frame);last=key;shot('ending_'..p..'_'..sub)end
  local r={frame,p,sub,movie~=0 and m.read32(movie+0x70)or -1,
   movie~=0 and m.read32(movie+0x6c)or -1,movie~=0 and m.read32(movie+0x74)or -1,
   staff~=0 and m.read32(staff+0x74)or -1,staff~=0 and m.read32(staff+0x80)or -1,
   staff~=0 and m.read32(staff+0x9c)or 0}
  for _,off in ipairs({0,4,8,12,16})do r[#r+1]=si(m.read32(0x8c21c090+off))end
  for _,off in ipairs({20,24,28})do r[#r+1]=rf(0x8c21c090+off)end
  r[#r+1]=m.read32(0x8c21c0b0);log:write(table.concat(r,',')..'\n');log:flush()
  if staff~=0 and m.read32(staff+0x80)==300 then shot('staff_300')end
  if p==4 and sub==2 then
   choice=choice+1
   if choice==20 and m.read32(0x8c1e55e0)~=0 then i.pressButtons(1,0x80)end
   if choice==40 then assert(m.read32(0x8c1e55e0)==0,'No was not selected');i.pressButtons(1,4)end
  end
 elseif started then
  log:close();print('ENDING complete ending parent deleted, selected No; frame='..frame);os.exit(0)
 end
 if frame>120000 then error('ending reference timed out')end
end}
