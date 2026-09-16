-- =====================================================================
-- SIMQ — esquema de base de datos
-- Ejecutar en el SQL Editor de Supabase.
-- =====================================================================

-- ---------------------------------------------------------------------
-- Catálogos
-- ---------------------------------------------------------------------

create table if not exists personal (
  id          bigserial primary key,
  nombre      text not null,
  rol         text not null,
  documento   text,
  activo      boolean default true,
  creado_en   timestamptz default now()
);

create table if not exists equipos (
  id          bigserial primary key,
  nombre      text not null,
  marca       text,
  modelo      text,
  serie       text,
  ubicacion   text,
  activo      boolean default true,
  creado_en   timestamptz default now()
);

create table if not exists insumos (
  id          bigserial primary key,
  nombre      text not null,
  tipo        text,          -- instrumental | insumo | medicamento
  unidad      text,
  activo      boolean default true
);

create table if not exists procedimientos (
  id          bigserial primary key,
  nombre      text not null,
  especialidad text
);

-- ---------------------------------------------------------------------
-- Operación
-- ---------------------------------------------------------------------

create table if not exists sesiones (
  id              bigserial primary key,
  device_id       text not null,
  quirofano       text,

  -- datos sensibles: ver políticas al final del archivo
  paciente_nombre text,
  paciente_doc    text,
  paciente_edad   int,
  paciente_sexo   text,
  diagnostico     text,

  procedimiento   text,
  descripcion     text,

  inicio          timestamptz,
  fin             timestamptz,
  duracion_seg    int,
  estado          text default 'en_curso',  -- en_curso | finalizada | anulada

  creado_por      uuid references auth.users(id),
  creado_en       timestamptz default now()
);

create table if not exists lecturas (
  id          bigserial primary key,
  device_id   text not null,
  ts          timestamptz not null default now(),
  temp        real,
  hum         real,
  lux         real,
  ruido       real
);

create index if not exists idx_lecturas_dev_ts
  on lecturas (device_id, ts desc);

create index if not exists idx_lecturas_ts
  on lecturas (ts desc);

create table if not exists eventos (
  id          bigserial primary key,
  sesion_id   bigint references sesiones(id) on delete cascade,
  ts          timestamptz not null default now(),
  tipo        text not null,   -- alarma | normal | paso | nota | inicio | pausa | fin
  variable    text,            -- temp | hum | lux | ruido
  valor       real,
  detalle     text
);

create index if not exists idx_eventos_sesion on eventos (sesion_id, ts);

-- ---------------------------------------------------------------------
-- Relaciones de la sesión
-- ---------------------------------------------------------------------

create table if not exists sesion_personal (
  sesion_id   bigint references sesiones(id) on delete cascade,
  personal_id bigint references personal(id),
  rol_sesion  text,
  primary key (sesion_id, personal_id)
);

create table if not exists sesion_equipos (
  sesion_id   bigint references sesiones(id) on delete cascade,
  equipo_id   bigint references equipos(id),
  primary key (sesion_id, equipo_id)
);

create table if not exists sesion_insumos (
  sesion_id   bigint references sesiones(id) on delete cascade,
  insumo_id   bigint references insumos(id),
  cantidad    int default 1,
  primary key (sesion_id, insumo_id)
);

create table if not exists sesion_pasos (
  id          bigserial primary key,
  sesion_id   bigint references sesiones(id) on delete cascade,
  orden       int not null,
  descripcion text not null,
  completado  boolean default false,
  hora        timestamptz
);

-- ---------------------------------------------------------------------
-- Umbrales configurables por quirófano
-- ---------------------------------------------------------------------

create table if not exists umbrales (
  id          bigserial primary key,
  quirofano   text,
  variable    text not null,
  minimo      real not null,
  maximo      real not null,
  unidad      text,
  fuente      text            -- norma o referencia que respalda el rango
);

insert into umbrales (quirofano, variable, minimo, maximo, unidad, fuente)
select * from (values
  ('default','temp',  18.0,  24.0, '°C',  'Por confirmar: Res. 3100/2019, ASHRAE 170'),
  ('default','hum',   30.0,  60.0, '%',   'Por confirmar: Res. 3100/2019, ASHRAE 170'),
  ('default','lux',  300.0, 500.0, 'lux', 'Por confirmar: iluminación general'),
  ('default','ruido',  0.0,  45.0, 'dB',  'Por confirmar: recomendación OMS')
) as v
where not exists (select 1 from umbrales where quirofano = 'default');

-- ---------------------------------------------------------------------
-- Seguridad
--
-- `lecturas` es la única tabla que la ESP32 necesita escribir con la
-- anon key. Todo lo demás exige usuario autenticado.
--
-- Las tablas con datos del paciente NO deben quedar accesibles con la
-- anon key pública. Ley 1581 de 2012.
-- ---------------------------------------------------------------------

alter table lecturas        enable row level security;
alter table sesiones        enable row level security;
alter table eventos         enable row level security;
alter table personal        enable row level security;
alter table equipos         enable row level security;
alter table insumos         enable row level security;
alter table procedimientos  enable row level security;
alter table sesion_personal enable row level security;
alter table sesion_equipos  enable row level security;
alter table sesion_insumos  enable row level security;
alter table sesion_pasos    enable row level security;
alter table umbrales        enable row level security;

-- El dispositivo solo inserta lecturas
create policy "dispositivo inserta lecturas"
  on lecturas for insert to anon
  with check (true);

-- Lectura de datos ambientales para usuarios autenticados
create policy "lecturas visibles autenticado"
  on lecturas for select to authenticated
  using (true);

-- Catálogos: lectura para autenticados
create policy "catalogo personal"       on personal       for select to authenticated using (true);
create policy "catalogo equipos"        on equipos        for select to authenticated using (true);
create policy "catalogo insumos"        on insumos        for select to authenticated using (true);
create policy "catalogo procedimientos" on procedimientos for select to authenticated using (true);
create policy "catalogo umbrales"       on umbrales       for select to authenticated using (true);

-- Sesiones: cada usuario ve y gestiona las suyas
create policy "sesiones propias select"
  on sesiones for select to authenticated
  using (creado_por = auth.uid());

create policy "sesiones propias insert"
  on sesiones for insert to authenticated
  with check (creado_por = auth.uid());

create policy "sesiones propias update"
  on sesiones for update to authenticated
  using (creado_por = auth.uid());

-- Eventos: atados a una sesión del usuario
create policy "eventos de sesiones propias"
  on eventos for all to authenticated
  using (exists (
    select 1 from sesiones s
    where s.id = eventos.sesion_id and s.creado_por = auth.uid()
  ));

-- ---------------------------------------------------------------------
-- Retención
--
-- Las lecturas crecen a 86.400 filas por día y dispositivo. Programar
-- una tarea que borre o agregue lo anterior a 90 días.
-- ---------------------------------------------------------------------

-- delete from lecturas where ts < now() - interval '90 days';
