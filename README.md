# LEM App — repositório do cliente Qt

Código do cliente em `app/`. Configurar o Qt Creator com `app/CMakeLists.txt` e o kit Qt 6.12/MinGW 15.1. Compilação normal: `LEM_WINDOWS_STANDALONE=OFF`.

Usar `../ongoing_work/` como diretório de trabalho e colocar builds, dependências, logs e ficheiros temporários nesse diretório. Exemplo, a partir de `ongoing_work`:

```powershell
cmake -S ../App/app -B app-build -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64 -DLEM_WINDOWS_STANDALONE=OFF
cmake --build app-build --parallel 4
python ../App/app/tests/test_general_notes.py
```

`shared/` é uma cópia versionada dos recursos canónicos de `../Common/shared/`. Permite compilar este repositório sem depender de outro clone. Alterar os recursos canónicos e executar `python ../Common/tools/sync_shared.py --write`, revendo e fazendo commit em cada repositório afetado. Sem `--write`, a ferramenta apenas verifica igualdade.

Documentação específica em `docs/`; arquitetura, contrato partilhado e guia da migração em `../Common/docs/`. Testes de integração com API usam o repositório irmão `Server` ou a variável `LEM_SERVER_REPO` (caminho para a raiz desse repositório).

Base importada: `Lem_Qt_0.1.57_GeneralNotesAndSignaturePhoto.zip`, incluindo as correções de compilação e todas as alterações funcionais aprovadas. Sem binários de distribuição neste repositório.
